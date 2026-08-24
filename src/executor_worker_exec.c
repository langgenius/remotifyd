#include "executor_worker_exec.h"

#include "executor_worker_exec_reader.h"
#include "executor_worker_shell_locate.h"
#include "ipc_response_types.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>

void executor_worker_exec_handle_closed(uv_handle_t *handle) {
    ExecutorCommand *command = handle->data;

    command->open_handles--;

    if (command->open_handles != 0) return;

    if (command->output_iconv != (iconv_t)-1) iconv_close(command->output_iconv);
    if (command->error_iconv != (iconv_t)-1) iconv_close(command->error_iconv);
    free(command);
}

bool executor_worker_exec_report(ExecutorCommand *command, IpcResponseType response_type, IpcExecReportHeader *report) {
    IpcHeader header = {
        .request_id = command->request_id,
        .body_length = (uint32_t)(sizeof(*report) + report->stdout_length + report->stderr_length),
        .type = IPC_REQUEST_EXEC,
        .response_type = response_type
    };

    /* The converted stdout and stderr bytes are contiguous after this header. */
    memcpy(report->operation_id, command->operation_id, IPC_OPERATION_ID_LENGTH);
    if (response_type != IPC_SINGLE) report->local_process_id = 0;
    report->error_length = 0;
    return executor_worker_output(command->worker, &header, (const uint8_t *)report);
}

void executor_worker_exec_terminal_event(ExecutorCommand *command) {
    IpcExecReportHeader report = {0};

    command->terminal_events--;
    if (command->terminal_events != 0) return;

    report.exit_code = command->exit_code;
    if (!executor_worker_exec_report(command, IPC_END, &report)) {
        logf_error("failed to report the ended command");
    }
}

static void executor_command_exited(uv_process_t *process, int64_t exit_status, int term_signal) {
    ExecutorCommand *command = process->data;

    (void)term_signal;

    command->exit_code = (int32_t)exit_status;
    executor_worker_exec_terminal_event(command);

    /* stdout and stderr remain alive until their readers receive UV_EOF. */
    uv_close((uv_handle_t *)process, executor_worker_exec_handle_closed);
}

static void executor_worker_exec_report_failed(
    ExecutorCommand *command,
    int error
) {
    const char *diagnostic = uv_strerror(error);
    size_t diagnostic_length = strlen(diagnostic);
    IpcExecReportHeader *report = calloc(1, sizeof(*report) + diagnostic_length);
    IpcHeader header = {
        .request_id = command->request_id,
        .body_length = (uint32_t)(sizeof(*report) + diagnostic_length),
        .type = IPC_REQUEST_EXEC,
        .response_type = IPC_SINGLE,
        .status = 1
    };

    if (report == NULL) return;

    memcpy(report->operation_id, command->operation_id, IPC_OPERATION_ID_LENGTH);
    report->error_length = (uint32_t)diagnostic_length;
    memcpy(report + 1, diagnostic, diagnostic_length);

    /* Preserve libuv's concrete spawn failure in the existing Exec report body. */
    executor_worker_output(command->worker, &header, (const uint8_t *)report);
    free(report);
}

static void executor_command_input_written(uv_write_t *write, int status) {
    uv_stream_t *input = write->handle;

    if (status < 0) {
        logf_error("failed to write command input: %s", uv_strerror(status));
    }

    /* uv_write has finished using both uv_write_t and the bytes after it. */
    free(write);

    /* stdin is one-shot; closing the pipe delivers EOF to the command. */
    uv_close((uv_handle_t *)input, executor_worker_exec_handle_closed);
}

static void executor_command_input(ExecutorCommand *command, const uint8_t *input, size_t input_length) {
    uv_write_t *write;
    uv_buf_t buffer;
    int result;

    if (input_length == 0) {
        uv_close((uv_handle_t *)&command->input, executor_worker_exec_handle_closed);
        return;
    }

    /* The copied bytes must remain alive until libuv invokes the callback. */
    write = malloc(sizeof(*write) + input_length);

    if (write == NULL) {
        logf_error("failed to allocate command input");
        uv_close((uv_handle_t *)&command->input, executor_worker_exec_handle_closed);
        return;
    }

    memcpy(write + 1, input, input_length);
    buffer = uv_buf_init((char *)(write + 1), (unsigned int)input_length);
    result = uv_write(write, (uv_stream_t *)&command->input, &buffer, 1, executor_command_input_written);

    if (result == 0) return;

    logf_error("failed to queue command input: %s", uv_strerror(result));
    free(write);
    uv_close((uv_handle_t *)&command->input, executor_worker_exec_handle_closed);
}

static bool executor_command_pipe(ExecutorCommand *command) {
    int result;

    result = uv_pipe_init(&command->worker->loop, &command->input, 0);

    if (result != 0) {
        logf_error("failed to initialize command input pipe: %s", uv_strerror(result));
        free(command);
        return false;
    }

    command->input.data = command;
    command->open_handles++;
    result = uv_pipe_init(&command->worker->loop, &command->output, 0);

    if (result != 0) {
        logf_error("failed to initialize command output pipe: %s", uv_strerror(result));
        uv_close((uv_handle_t *)&command->input, executor_worker_exec_handle_closed);
        return false;
    }

    command->output.data = command;
    command->open_handles++;
    result = uv_pipe_init(&command->worker->loop, &command->error, 0);

    if (result != 0) {
        logf_error("failed to initialize command error pipe: %s", uv_strerror(result));
        uv_close((uv_handle_t *)&command->input, executor_worker_exec_handle_closed);
        uv_close((uv_handle_t *)&command->output, executor_worker_exec_handle_closed);
        return false;
    }

    command->error.data = command;

    /*
     * Each initialized pipe is a live libuv handle. Later cleanup uses this
     * count so ExecutorCommand cannot be released while a pipe still exists.
     */
    command->open_handles++;
    return true;
}

void executor_worker_handle_exec_request(
    ExecutorWorker *worker,
    const IpcHeader *header,
    const IpcExecRequestHeader *request
) {
    const char *file = executor_worker_shell_path();
    const uint8_t *cmd_bytes = (const uint8_t *)(request + 1);
    const uint8_t *cwd_bytes = cmd_bytes + request->cmd_length;
    const uint8_t *input_bytes = cwd_bytes + request->cwd_length;
    ExecutorCommand *command;
    uv_stdio_container_t stdio[3] = {0};
    uv_process_options_t options = {0};
    char *cmd;
    char *cwd;
    int result;

#ifdef _WIN32
    ExecutorShellType shell_type = executor_worker_shell_type();
    char *arguments[7];
#else
    char *arguments[4];
#endif

    command = calloc(1, sizeof(*command));
    cmd = malloc((size_t)request->cmd_length + 1);
    cwd = malloc((size_t)request->cwd_length + 1);

    if (command == NULL || cmd == NULL || cwd == NULL) {
        logf_error("failed to allocate command");
        free(cwd);
        free(cmd);
        free(command);
        return;
    }

    memcpy(cmd, cmd_bytes, request->cmd_length);
    cmd[request->cmd_length] = '\0';
    memcpy(cwd, cwd_bytes, request->cwd_length);
    cwd[request->cwd_length] = '\0';

    command->worker = worker;
    command->request_id = header->request_id;
    memcpy(command->operation_id, request->operation_id, IPC_OPERATION_ID_LENGTH);
    command->terminal_events = 3;
    command->output_iconv = (iconv_t)-1;
    command->error_iconv = (iconv_t)-1;

    if (!executor_command_pipe(command)) {
        free(cwd);
        free(cmd);
        return;
    }

    stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
    stdio[0].data.stream = (uv_stream_t *)&command->input;
    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t *)&command->output;
    stdio[2].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[2].data.stream = (uv_stream_t *)&command->error;

#ifdef _WIN32
    if (shell_type == EXECUTOR_SHELL_POWERSHELL) {
        arguments[0] = (char *)file;
        arguments[1] = "-NoLogo";
        arguments[2] = "-NoProfile";
        arguments[3] = "-NonInteractive";
        arguments[4] = "-Command";
        arguments[5] = cmd;
        arguments[6] = NULL;
    } else {
        arguments[0] = (char *)file;
        arguments[1] = "/D";
        arguments[2] = "/S";
        arguments[3] = "/C";
        arguments[4] = cmd;
        arguments[5] = NULL;
    }
#else
    arguments[0] = (char *)file;
    arguments[1] = "-c";
    arguments[2] = cmd;
    arguments[3] = NULL;
#endif

    options.file = file;
    options.args = arguments;
    options.cwd = request->cwd_length == 0 ? NULL : cwd;
    options.exit_cb = executor_command_exited;
    options.stdio = stdio;
    options.stdio_count = 3;

    command->process.data = command;
    result = uv_spawn(&worker->loop, &command->process, &options);

    /* uv_spawn initializes the process handle even when the child cannot start. */
    command->open_handles++;

    /* uv_spawn consumes file, args, and cwd before returning. */
    free(cwd);
    free(cmd);

    if (result != 0) {
        logf_error("failed to start command: %s", uv_strerror(result));
        executor_worker_exec_report_failed(command, result);
        uv_close((uv_handle_t *)&command->process, executor_worker_exec_handle_closed);
        uv_close((uv_handle_t *)&command->input, executor_worker_exec_handle_closed);
        uv_close((uv_handle_t *)&command->output, executor_worker_exec_handle_closed);
        uv_close((uv_handle_t *)&command->error, executor_worker_exec_handle_closed);
        return;
    }

    {
        IpcExecReportHeader report = {
            .local_process_id = (uint64_t)command->process.pid
        };

        /* The caller receives the real local process ID before any output. */
        if (!executor_worker_exec_report(command, IPC_SINGLE, &report)) {
            logf_error("failed to report the started command");
        }
    }

    /* uv_spawn has now created and connected both output pipes. */
    result = uv_read_start((uv_stream_t *)&command->output, executor_worker_exec_alloc, executor_worker_exec_stdout_read);

    if (result != 0) {
        logf_error("failed to start command output reader: %s", uv_strerror(result));
        executor_worker_exec_stdout_end(command);
    }

    result = uv_read_start((uv_stream_t *)&command->error, executor_worker_exec_alloc, executor_worker_exec_stderr_read);

    if (result != 0) {
        logf_error("failed to start command error reader: %s", uv_strerror(result));
        executor_worker_exec_stderr_end(command);
    }

    executor_command_input(command, input_bytes, request->standard_input_length);
}
