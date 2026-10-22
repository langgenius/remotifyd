#include "websocket_ioloop.h"

#include "circular_queue.h"
#include "logger.h"
#include "websocket_decode.h"
#include "websocket_eventloop.h"
#include "websocket_send.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { WEBSOCKET_READ_BYTES = 128 * 1024 };

void websocket_write(uv_timer_t *timer) {
    EventLoop *event_loop = timer->data;
    CircularQueue *queue = &event_loop->websocket_send_queue;
    WebSocketSend *send;
    CURLcode result;
    int poll_result;

    while ((send = circular_queue_get(queue)) != NULL) {
        result = websocket_send(event_loop, send);

        if (result == CURLE_AGAIN) {
            /*
             * Keep the current queue head and its sent offset intact. libuv
             * will wake the same writer after this socket becomes writable.
             */
            poll_result = uv_poll_start(
                &event_loop->websocket_watch->poll,
                UV_READABLE | UV_WRITABLE | UV_DISCONNECT,
                websocket_socket_event
            );

            if (poll_result != 0) {
                logf_error(
                    "failed to watch WebSocket writability: %s",
                    uv_strerror(poll_result)
                );
                close_websocket_socket(event_loop);
            }

            return;
        }

        if (result != CURLE_OK) {
            logf_error(
                "failed to send WebSocket message: %s",
                curl_easy_strerror(result)
            );
            close_websocket_socket(event_loop);
            return;
        }

        if (send->sent < send->length) continue;

        send = circular_queue_consume(queue);
        send->consumed_callback(send, true);
        free(send);
    }
}

static void websocket_read(EventLoop *event_loop) {
    uint8_t bytes[WEBSOCKET_READ_BYTES];
    const struct curl_ws_frame *frame;
    size_t message_offset;
    size_t received;
    CURLcode result;
    bool decoded;

    for (;;) {
        result = curl_ws_recv(
            event_loop->websocket,
            bytes,
            sizeof(bytes),
            &received,
            &frame
        );

        /* Wait for another uv event only after curl has drained TLS buffers. */
        if (result == CURLE_AGAIN) return;

        if (result == CURLE_GOT_NOTHING) goto websocket_closed;

        if (result != CURLE_OK) {
            logf_error(
                "failed to read WebSocket: %s",
                curl_easy_strerror(result)
            );
            close_websocket_socket(event_loop);
            return;
        }

        /* Any received frame proves the connection still has a live peer. */
        event_loop->last_rx_ms = uv_now(&event_loop->loop);

        if ((frame->flags & CURLWS_CLOSE) != 0) goto websocket_closed;

        /* libcurl answers PING automatically; neither control frame is RPC. */
        if ((frame->flags & (CURLWS_PING | CURLWS_PONG)) != 0) continue;

        if ((frame->flags & CURLWS_TEXT) != 0) {
            logf_error("received a text WebSocket message");
            close_websocket_socket(event_loop);
            return;
        }

        message_offset = event_loop->websocket_message_length;
        event_loop->websocket_message_length += received;

        /*
         * Oversized messages are still consumed to their ordinary WebSocket
         * boundary, but bytes beyond the fixed protocol limit are not copied.
         */
        if (event_loop->websocket_message_length <=
            sizeof(event_loop->websocket_message)) {
            memcpy(
                event_loop->websocket_message + message_offset,
                bytes,
                received
            );
        }

        /*
         * bytesleft ends the current frame. CURLWS_CONT means another frame
         * still belongs to the same binary WebSocket message.
         */
        if (frame->bytesleft != 0 ||
            (frame->flags & CURLWS_CONT) != 0) continue;

        if (event_loop->websocket_message_length >
            sizeof(event_loop->websocket_message)) {
            logf_error("WebSocket message exceeds 128 KiB");
        } else {
            decoded = websocket_decode(
                event_loop,
                event_loop->websocket_message,
                event_loop->websocket_message_length
            );

            if (!decoded) logf_error("failed to decode WebSocket message");
        }

        event_loop->websocket_message_length = 0;
    }

websocket_closed:
    /*
     * Only the read side observes the final connection end and schedules the
     * one cleanup for this WebSocket lifecycle.
     */
    uv_poll_stop(&event_loop->websocket_watch->poll);
    schedule_cleanup_websocket(event_loop);
}

void websocket_socket_event(
    uv_poll_t *poll,
    int status,
    int events
) {
    CurlSocketWatch *watch = poll->data;
    EventLoop *event_loop = watch->event_loop;
    int result;

    if (status < 0) {
        logf_error(
            "failed to poll WebSocket: %s",
            uv_strerror(status)
        );
        close_websocket_socket(event_loop);
        return;
    }

    if ((events & UV_DISCONNECT) != 0) {
        /*
         * The socket callback owns uv event state. Stopping the poll here
         * guarantees one cleanup for this connection lifecycle.
         */
        uv_poll_stop(poll);
        schedule_cleanup_websocket(event_loop);
        return;
    }

    if ((events & UV_WRITABLE) != 0) {
        /*
         * Writable is level-triggered. Stop watching it before scheduling one
         * send attempt, then add it again only if curl returns CURLE_AGAIN.
         */
        result = uv_poll_start(poll, UV_READABLE | UV_DISCONNECT, websocket_socket_event);

        if (result != 0) {
            logf_error(
                "failed to stop watching WebSocket writability: %s",
                uv_strerror(result)
            );
            close_websocket_socket(event_loop);
            return;
        }

        result = uv_timer_start(&event_loop->websocket_send_timer, websocket_write, 0, 0);

        if (result != 0) {
            /*
             * The process-owned send timer remains valid for EventLoop's
             * entire lifetime, so this fixed one-shot start cannot normally
             * fail. There is deliberately no fallback send path.
             */
            logf_error(
                "failed to resume WebSocket write: %s",
                uv_strerror(result)
            );
        }
    }

    if ((events & UV_READABLE) != 0) websocket_read(event_loop);
}
