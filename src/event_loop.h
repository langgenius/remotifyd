#ifndef REMOTIFY_EVENT_LOOP_H
#define REMOTIFY_EVENT_LOOP_H

#include "circular_queue.h"
#include "device_protocol_gen.h"
#include "ipc_types.h"
#include "state.h"

#include <stdbool.h>
#include <curl/curl.h>
#include <stdint.h>
#include <uv.h>

typedef struct EventLoop EventLoop;

typedef struct CurlSocketWatch {
    uv_poll_t poll;
    EventLoop *event_loop;
    curl_socket_t socket;
    struct CurlSocketWatch *next;
} CurlSocketWatch;

/*
 * EventLoop is allocated by main and remains alive until uv_run returns.
 * Every asynchronous handle can therefore point back to this one owner
 * without introducing globals or separately allocated callback contexts.
 */
struct EventLoop {
    uv_loop_t loop;
    State state;

    /* Connection advancement, cleanup, and reconnect reuse this one timer. */
    uv_timer_t timer;

    /* Independent upper bound for one WebSocket upgrade attempt. */
    uv_timer_t handshake_timer;

    /* Independent liveness deadline for an established WebSocket. */
    uv_timer_t liveness_timer;
    uint64_t last_rx_ms;

    uv_process_t executor;
    uv_pipe_t executor_input;
    uv_pipe_t executor_output;
    IpcReadBuffer executor_read;

    IpcProcessCallbacks callbacks;

    /* libcurl can publish several connection candidates before one wins. */
    CurlSocketWatch *curl_socket_watches;
    CurlSocketWatch *websocket_watch;
    uv_close_cb socket_watches_closed;

    /*
     * The queue preserves frame order while a partial curl write waits for
     * UV_WRITABLE. One timer resumes its head outside the poll callback; the
     * other periodically produces the application Heartbeat request.
     */
    uv_timer_t websocket_send_timer;
    uv_timer_t heartbeat_timer;
    CircularQueue websocket_send_queue;
    CURLM *multi;
    CURL *websocket;
    uint8_t websocket_message[DEVICE_MAXIMUM_FRAME_BYTES];
    size_t websocket_message_length;
    uint64_t next_request_id;
};

int event_loop_run(EventLoop *event_loop);
void connect_remotify(uv_timer_t *timer);
void schedule_cleanup_websocket(EventLoop *event_loop);

void event_loop_executor_output_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer);
void event_loop_executor_output_read(uv_stream_t *stream, ssize_t received, const uv_buf_t *buffer);
void event_loop_executor_handle(
    EventLoop *event_loop,
    uint32_t device_request_id,
    const IpcHeader *header,
    const uint8_t *body
);

#endif
