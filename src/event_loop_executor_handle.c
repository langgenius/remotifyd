#include "event_loop.h"

#include "ipc_request_types.h"
#include "ipc_response_types.h"
#include "logger.h"
#include "websocket_send.h"

/*
 * IpcProcessCallbacks invokes this function only after the generic pipe loop
 * has assembled one complete Exec response. This callback translates that
 * operation-specific body without moving Exec knowledge into the read loop.
 */
void event_loop_executor_handle(
    EventLoop *event_loop,
    uint32_t device_request_id,
    const IpcHeader *header,
    const uint8_t *body
) {
    const IpcExecReportHeader *report;

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
