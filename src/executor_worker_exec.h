#ifndef REMOTIFY_EXECUTOR_WORKER_EXEC_H
#define REMOTIFY_EXECUTOR_WORKER_EXEC_H

#include "executor_worker.h"
#include "ipc_response_types.h"

#include <iconv.h>

enum { EXECUTOR_WORKER_EXEC_PENDING_BYTES = 16 };

/*
 * One ExecutorCommand owns one real command process and its three stdio
 * pipes. Process exit and the two output EOFs each consume one terminal
 * event; open_handles independently decides when the allocation can be freed.
 */
typedef struct ExecutorCommand {
    ExecutorWorker *worker;
    uint64_t request_id;
    char operation_id[IPC_OPERATION_ID_LENGTH];

    uv_process_t process;
    uv_pipe_t input;
    uv_pipe_t output;
    uv_pipe_t error;

    uint8_t terminal_events;
    uint8_t open_handles;
    int32_t exit_code;

    iconv_t output_iconv;
    uint8_t output_pending[EXECUTOR_WORKER_EXEC_PENDING_BYTES];
    size_t output_pending_length;
    bool output_started;
    bool output_hex;

    iconv_t error_iconv;
    uint8_t error_pending[EXECUTOR_WORKER_EXEC_PENDING_BYTES];
    size_t error_pending_length;
    bool error_started;
    bool error_hex;
} ExecutorCommand;

void executor_worker_exec_handle_closed(uv_handle_t *handle);
bool executor_worker_exec_report(ExecutorCommand *command, IpcResponseType response_type, IpcExecReportHeader *report);
void executor_worker_exec_terminal_event(ExecutorCommand *command);

#endif
