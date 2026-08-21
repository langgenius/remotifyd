#include "websocket_send.h"

#include "circular_queue.h"
#include "device_protocol_gen.h"
#include "logger.h"
#include "websocket_ioloop.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/* curl headers can expose stdio's macros, which collide with RPC field names. */
#ifdef stdout
#undef stdout
#endif

#ifdef stderr
#undef stderr
#endif

CURLcode websocket_send(EventLoop *event_loop, WebSocketSend *send) {
    size_t sent = 0;
    CURLcode result = curl_ws_send(
        event_loop->websocket,
        send->bytes + send->sent,
        send->length - send->sent,
        &sent,
        0,
        CURLWS_BINARY
    );

    send->sent += sent;
    return result;
}

static void websocket_exec_report_consumed(
    WebSocketSend *send,
    bool succeed
) {
    (void)succeed;
    free(send->bytes);
}

static bool websocket_send_put(EventLoop *event_loop, WebSocketSend *send) {
    CircularQueue *queue = &event_loop->websocket_send_queue;
    bool was_empty = queue->length == 0;
    int result;

    if (!circular_queue_put(queue, send)) return false;
    if (!was_empty) return true;

    /* The first queued message starts the one-shot asynchronous writer. */
    result = uv_timer_start(&event_loop->websocket_send_timer, websocket_write, 0, 0);

    if (result != 0) {
        /*
         * This process-owned timer is initialized once and never closed while
         * EventLoop is running, so this call cannot normally fail. Recording
         * the violated invariant is preferable to adding another send path.
         */
        logf_error(
            "failed to schedule WebSocket write: %s",
            uv_strerror(result)
        );
    }

    return true;
}

bool websocket_send_exec_report(
    EventLoop *event_loop,
    const IpcExecReportHeader *report,
    bool ended
) {
    const uint8_t *stdout_bytes = (const uint8_t *)(report + 1);
    const uint8_t *stderr_bytes = stdout_bytes + report->stdout_length;
    DeviceExecReportRequestV1 message = {
        .operation_id = (const uint8_t *)report->operation_id,
        .operation_id_length = IPC_OPERATION_ID_LENGTH,
        .stdout = stdout_bytes,
        .stdout_length = report->stdout_length,
        .stderr = stderr_bytes,
        .stderr_length = report->stderr_length,
        .has_exit_code = ended,
        .exit_code = report->exit_code
    };
    WebSocketSend *send = malloc(sizeof(*send));

    if (send == NULL) return false;

    send->bytes = malloc(DEVICE_MAXIMUM_FRAME_BYTES);

    if (send->bytes == NULL) {
        free(send);
        return false;
    }

    send->sent = 0;
    send->consumed_callback = websocket_exec_report_consumed;

    /* Encode directly into the owned send buffer; no frame copy is needed. */
    if (!device_encode_exec_report_request_v1(
            send->bytes,
            DEVICE_MAXIMUM_FRAME_BYTES,
            &send->length,
            (uint32_t)event_loop->next_request_id++,
            &message
        )) {
        send->consumed_callback(send, false);
        free(send);
        return false;
    }

    if (!websocket_send_put(event_loop, send)) {
        send->consumed_callback(send, false);
        free(send);
        return false;
    }

    return true;
}

void websocket_send_clear(EventLoop *event_loop) {
    WebSocketSend *send;

    uv_timer_stop(&event_loop->websocket_send_timer);

    /* Unsent frames belong to the failed connection and are never retried. */
    while ((send = circular_queue_consume(
                &event_loop->websocket_send_queue
            )) != NULL) {
        send->consumed_callback(send, false);
        free(send);
    }
}
