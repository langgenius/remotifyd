#include "executor_worker.h"

#include "logger.h"

#include <stdbool.h>
#include <stdlib.h>

static bool executor_worker_pipe(ExecutorWorker *worker) {
    int result;

    result = uv_pipe_init(&worker->loop, &worker->input, 0);

    if (result != 0) {
        logf_error(
            "failed to initialize Executor worker input pipe: %s",
            uv_strerror(result)
        );
        return false;
    }

    result = uv_pipe_open(&worker->input, 0);

    if (result != 0) {
        logf_error(
            "failed to open Executor worker input pipe: %s",
            uv_strerror(result)
        );
        return false;
    }

    result = uv_pipe_init(&worker->loop, &worker->output, 0);

    if (result != 0) {
        logf_error(
            "failed to initialize Executor worker output pipe: %s",
            uv_strerror(result)
        );
        return false;
    }

    result = uv_pipe_open(&worker->output, 1);

    if (result != 0) {
        logf_error(
            "failed to open Executor worker output pipe: %s",
            uv_strerror(result)
        );
        return false;
    }

    /*
     * libuv supplies only the stream to a read callback. The stream points to
     * its child-owned ExecutorWorker so the callback never reaches into the
     * parent process EventLoop.
     */
    worker->input.data = worker;

    result = uv_read_start(
        (uv_stream_t *)&worker->input,
        executor_worker_input_alloc,
        executor_worker_input_read
    );

    if (result != 0) {
        logf_error(
            "failed to watch Executor parent input: %s",
            uv_strerror(result)
        );
        return false;
    }

    /*
     * The parent created stdin as the child-readable IPC pipe and stdout as
     * the child-writable IPC pipe. The worker adopts those inherited handles
     * into its own loop instead of creating another pair of pipes.
     */
    return true;
}

int executor_worker_run(void) {
    ExecutorWorker worker = {0};
    int result;

    /*
     * The child process owns this loop for its complete lifetime. Keeping the
     * structure on the child entry stack makes every later Executor callback
     * refer to child-owned state rather than the parent's EventLoop.
     */
    result = uv_loop_init(&worker.loop);

    if (result != 0) {
        /**
         * Normally, it's not accessable here.
         */
        logf_error(
            "failed to initialize Executor event loop: %s",
            uv_strerror(result)
        );
        return EXIT_FAILURE;
    }

    if (!executor_worker_pipe(&worker)) {
        return EXIT_FAILURE;
    }

    return uv_run(&worker.loop, UV_RUN_DEFAULT);
}
