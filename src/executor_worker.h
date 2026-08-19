#ifndef REMOTIFY_EXECUTOR_WORKER_H
#define REMOTIFY_EXECUTOR_WORKER_H

#include "ipc_request_types.h"
#include "ipc_types.h"

#include <stdbool.h>
#include <uv.h>

/*
 * ExecutorWorker belongs exclusively to the child process. Its loop, pipe
 * handles, and partial IPC frame are separate from the parent EventLoop; the
 * two processes share bytes through the pipes, never in-memory state.
 */
typedef struct ExecutorWorker {
    uv_loop_t loop;

    uv_pipe_t input;
    uv_pipe_t output;
    IpcReadBuffer input_read;
} ExecutorWorker;

void executor_worker_input_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer);
void executor_worker_input_read(uv_stream_t *stream, ssize_t received, const uv_buf_t *buffer);
void executor_worker_handle(ExecutorWorker *worker, const IpcHeader *header, const uint8_t *body);
void executor_worker_handle_exec_request(ExecutorWorker *worker, const IpcHeader *header, const IpcExecRequestHeader *request);
bool executor_worker_output(ExecutorWorker *worker, const IpcHeader *header, const uint8_t *body);
int executor_worker_run(void);

#endif
