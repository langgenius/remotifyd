#include "event_loop.h"

#include "ipc_request_types.h"
#include "ipc_response_types.h"
#include "logger.h"
#include "websocket_send.h"

/*
 * IpcProcessCallbacks invokes this function only after the generic pipe loop
 * has assembled one complete operation response. This callback translates
 * that body without moving operation-specific knowledge into the read loop.
 */
void event_loop_executor_handle(
    EventLoop *event_loop,
    uint32_t device_request_id,
    const IpcHeader *header,
    const uint8_t *body
) {
    const IpcExecReportHeader *report;

    if (header->type == IPC_REQUEST_PATCH) {
        if (!websocket_send_patch_response(
                event_loop,
                device_request_id,
                (IpcStatus)header->status
            )) {
            logf_error("failed to queue patch result for WebSocket");
        }
        ipc_process_callbacks_remove(&event_loop->callbacks, device_request_id);
        return;
    }

    if (header->type == IPC_REQUEST_FILE_STAT) {
        IpcStatus status = (IpcStatus)header->status;

        if (status == IPC_STATUS_OK && header->body_length != sizeof(IpcFileStatResponse)) {
            status = IPC_STATUS_FAILED;
        }
        if (!websocket_send_file_stat_response(
                event_loop,
                device_request_id,
                status,
                (const IpcFileStatResponse *)body
            )) {
            logf_error("failed to queue file stat result for WebSocket");
        }
        ipc_process_callbacks_remove(&event_loop->callbacks, device_request_id);
        return;
    }

    if (header->type == IPC_REQUEST_FILE_UPLOAD ||
        header->type == IPC_REQUEST_FILE_DOWNLOAD) {
        if (!websocket_send_file_transfer_response(
                event_loop,
                device_request_id,
                (IpcRequestType)header->type,
                (IpcStatus)header->status
            )) {
            logf_error("failed to queue file transfer result for WebSocket");
        }
        ipc_process_callbacks_remove(&event_loop->callbacks, device_request_id);
        return;
    }

    if (header->type != IPC_REQUEST_EXEC) return;

    report = (const IpcExecReportHeader *)body;

    if (header->status != 0) {
        if (!websocket_send_exec_failure(
                event_loop,
                device_request_id,
                (const uint8_t *)(report + 1),
                report->error_length
            )) {
            logf_error("failed to queue Executor failure for WebSocket");
        }

        ipc_process_callbacks_remove(&event_loop->callbacks, device_request_id);
        return;
    }

    if (!websocket_send_exec_report(
            event_loop,
            device_request_id,
            report,
            header->response_type == IPC_END
        )) {
        logf_error("failed to queue Executor report for WebSocket");
    }

    if (header->response_type == IPC_END) {
        ipc_process_callbacks_remove(&event_loop->callbacks, device_request_id);
    }
}
