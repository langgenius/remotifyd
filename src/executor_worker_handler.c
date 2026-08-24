#include "executor_worker.h"

/*
 * The read loop calls this function only after one complete IPC frame has
 * arrived. The fixed header selects one operation-specific handler so pipe
 * buffering and operation implementations remain separate responsibilities.
 */
void executor_worker_handle(
    ExecutorWorker *worker,
    const IpcHeader *header,
    const uint8_t *body
) {
    if (header->type == IPC_REQUEST_EXEC) {
        executor_worker_handle_exec_request(worker, header, (const IpcExecRequestHeader *)body);
    } else if (header->type == IPC_REQUEST_PATCH) {
        executor_worker_handle_patch_request(worker, header, (const IpcPatchRequestHeader *)body);
    } else if (header->type == IPC_REQUEST_FILE_UPLOAD ||
               header->type == IPC_REQUEST_FILE_DOWNLOAD) {
        executor_worker_handle_file_transfer_request(
            worker, header, (const IpcFileTransferRequestHeader *)body
        );
    } else if (header->type == IPC_REQUEST_FILE_STAT) {
        executor_worker_handle_file_stat_request(
            worker, header, (const IpcFileStatRequestHeader *)body
        );
    }
}
