#ifndef REMOTIFY_EXECUTOR_WORKER_EXEC_READER_H
#define REMOTIFY_EXECUTOR_WORKER_EXEC_READER_H

#include "executor_worker_exec.h"

#include <uv.h>

void executor_worker_exec_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer);
void executor_worker_exec_stdout_end(ExecutorCommand *command);
void executor_worker_exec_stderr_end(ExecutorCommand *command);
void executor_worker_exec_stdout_read(uv_stream_t *stream, ssize_t received, const uv_buf_t *buffer);
void executor_worker_exec_stderr_read(uv_stream_t *stream, ssize_t received, const uv_buf_t *buffer);

#endif
