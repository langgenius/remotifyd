#ifndef REMOTIFY_WEBSOCKET_SEND_H
#define REMOTIFY_WEBSOCKET_SEND_H

#include "event_loop.h"
#include "ipc_response_types.h"

#include <stdbool.h>
#include <curl/curl.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WebSocketSend WebSocketSend;
typedef void (*WebSocketConsumedCallback)(WebSocketSend *send, bool succeed);

struct WebSocketSend {
    uint8_t *bytes;
    size_t length;
    size_t sent;
    WebSocketConsumedCallback consumed_callback;
};

CURLcode websocket_send(EventLoop *event_loop, WebSocketSend *send);
bool websocket_send_exec_report(
    EventLoop *event_loop,
    uint32_t request_id,
    const IpcExecReportHeader *report,
    bool ended
);
bool websocket_send_exec_failure(
    EventLoop *event_loop,
    uint32_t request_id,
    const uint8_t *diagnostic,
    size_t diagnostic_length
);
bool websocket_send_patch_response(
    EventLoop *event_loop,
    uint32_t request_id,
    IpcStatus status
);
void websocket_send_heartbeat(uv_timer_t *timer);
void websocket_send_clear(EventLoop *event_loop);

#endif
