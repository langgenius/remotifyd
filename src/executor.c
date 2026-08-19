#include "event_loop.h"

#include "logger.h"

#include <stdbool.h>

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
