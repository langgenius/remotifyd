#include "event_loop.h"

#include "ipc_request_types.h"
#include "ipc_response_types.h"
#include "logger.h"
#include "websocket_send.h"

/*
 * The read loop calls this function only after one complete Executor response
 * has arrived. Callback lookup and response handling belong here so pipe
 * buffering remains independent from the request that produced the response.
 */
void event_loop_executor_handle(
    EventLoop *event_loop,
    const IpcHeader *header,
    const uint8_t *body
) {
    const IpcExecReportHeader *report;

    if (header->type != IPC_REQUEST_EXEC) return;

    report = (const IpcExecReportHeader *)body;

    if (!websocket_send_exec_report(
            event_loop,
            report,
            header->response_type == IPC_END
        )) {
        logf_error("failed to queue Executor report for WebSocket");
    }
}
