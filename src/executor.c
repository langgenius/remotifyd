#include "executor.h"

#include "logger.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static void executor_request_written(uv_write_t *write, int status) {
    if (status < 0) {
        logf_error(
            "failed to write request to Executor: %s",
            uv_strerror(status)
        );
    }

    /* uv_write no longer uses the copied header or body after this callback. */
    free(write);
}

bool dispatch_request_to_executor(
    EventLoop *event_loop,
    const IpcHeader *request,
    const uint8_t *body
) {
    uv_write_t *write;
    IpcHeader *header_bytes;
    uint8_t *body_bytes;
    uv_buf_t buffers[2];
    unsigned int buffer_count;
    int result;

    /*
     * uv_write is asynchronous, so the complete frame is copied after its
     * request object and remains alive until executor_request_written runs.
     */
    write = malloc(sizeof(*write) + sizeof(*request) + request->body_length);

    if (write == NULL) return false;

    header_bytes = (IpcHeader *)(write + 1);
    body_bytes = (uint8_t *)(header_bytes + 1);
    *header_bytes = *request;

    if (request->body_length != 0) {
        memcpy(body_bytes, body, request->body_length);
    }

    buffers[0] = uv_buf_init((char *)header_bytes, sizeof(*header_bytes));
    buffers[1] = uv_buf_init((char *)body_bytes, request->body_length);
    buffer_count = request->body_length == 0 ? 1 : 2;

    result = uv_write(
        write,
        (uv_stream_t *)&event_loop->executor_input,
        buffers,
        buffer_count,
        executor_request_written
    );

    if (result != 0) {
        free(write);
        return false;
    }

    return true;
}

static void executor_exited(
    uv_process_t *executor,
    int64_t exit_status,
    int term_signal
) {
    (void)exit_status;
    (void)term_signal;

    /*
     * Process exit does not close its libuv handle. Closing it here lets the
     * loop finish naturally after the Executor has actually been reaped.
     */
    uv_close((uv_handle_t *)executor, NULL);
}

bool launch_executor(
    EventLoop *event_loop,
    const char *executable
) {
    uv_process_options_t options = {0};
    uv_stdio_container_t stdio[3] = {0};
    char *arguments[] = {
        (char *)executable,
        "executor",
        NULL
    };
    int result;

    result = uv_pipe_init(&event_loop->loop, &event_loop->executor_input, 0);

    if (result != 0) {
        logf_error(
            "failed to initialize Executor input pipe: %s",
            uv_strerror(result)
        );
        return false;
    }

    result = uv_pipe_init(&event_loop->loop, &event_loop->executor_output, 0);

    if (result != 0) {
        logf_error(
            "failed to initialize Executor output pipe: %s",
            uv_strerror(result)
        );
        return false;
    }

    /*
     * The child reads IPC requests from stdin, so its stdin is the readable
     * end of executor_input. The child writes IPC responses to stdout, so its
     * stdout is the writable end of executor_output. stderr remains inherited
     * and is reserved for operational logs rather than IPC bytes.
     */
    stdio[0].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
    stdio[0].data.stream = (uv_stream_t *)&event_loop->executor_input;

    stdio[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[1].data.stream = (uv_stream_t *)&event_loop->executor_output;

    stdio[2].flags = UV_INHERIT_FD;
    stdio[2].data.fd = 2;

    /*
     * Main and Executor use the same release artifact. The internal argument
     * selects the child role without introducing another executable or a
     * wrapper around the one real Executor process.
     */
    options.file = executable;
    options.args = arguments;
    options.exit_cb = executor_exited;
    options.stdio = stdio;
    options.stdio_count = 3;

    result = uv_spawn(
        &event_loop->loop,
        &event_loop->executor,
        &options
    );

    if (result != 0) {
        logf_error(
            "failed to start Executor: %s",
            uv_strerror(result)
        );
        return false;
    }

    /*
     * libuv supplies only the stream to the read callback. Pointing the
     * Executor output pipe at its EventLoop gives the callback access to the
     * one parent-owned partial frame without adding another context object.
     */
    event_loop->executor_output.data = event_loop;

    result = uv_read_start(
        (uv_stream_t *)&event_loop->executor_output,
        event_loop_executor_output_alloc,
        event_loop_executor_output_read
    );

    if (result != 0) {
        logf_error("failed to watch Executor output: %s", uv_strerror(result));
        return false;
    }

    return true;
}
