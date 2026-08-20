#include "websocket_eventloop.h"

#include "event_loop.h"
#include "logger.h"
#include "websocket_readloop.h"

void cleanup_websocket_resource(
    EventLoop *event_loop,
    uv_close_cb after_cleaned_cb
) {
    uv_poll_stop(&event_loop->websocket_poll);
    uv_close(
        (uv_handle_t *)&event_loop->websocket_poll,
        after_cleaned_cb
    );
}

void check_and_perform_socket_state_machine(EventLoop *event_loop) {
    CURLMsg *message;
    int messages_left;
    int result;

    while ((message = curl_multi_info_read(
                event_loop->multi,
                &messages_left
            )) != NULL) {
        if (message->msg != CURLMSG_DONE ||
            message->easy_handle != event_loop->websocket) {
            continue;
        }

        if (message->data.result != CURLE_OK) {
            logf_error(
                "failed to connect WebSocket: %s",
                curl_easy_strerror(message->data.result)
            );
            schedule_cleanup_websocket(event_loop);
            return;
        }

        /*
         * TCP, TLS, and WebSocket Upgrade are complete. The same poll now
         * stops driving curl multi and starts reading WebSocket payloads.
         */
        result = uv_poll_start(
            &event_loop->websocket_poll,
            UV_READABLE,
            websocket_read
        );

        if (result != 0) {
            logf_error(
                "failed to start WebSocket readloop: %s",
                uv_strerror(result)
            );
            schedule_cleanup_websocket(event_loop);
        }

        return;
    }
}

/*
 * libuv calls this function when it detects that websocket_poll has changed
 * status. The reported event triggers libcurl to continue its internal state
 * machine from the point where it previously had to wait for the socket.
 */
static void websocket_socket_event(
    uv_poll_t *poll,
    int status,
    int events
) {
    EventLoop *event_loop = poll->data;
    int action = 0;
    int running_handles = 0;
    CURLMcode result;

    if (status < 0) {
        action = CURL_CSELECT_ERR;
    } else {
        if ((events & UV_READABLE) != 0) action |= CURL_CSELECT_IN;
        if ((events & UV_WRITABLE) != 0) action |= CURL_CSELECT_OUT;
    }

    /*
     * libuv only reports readiness. Passing the event back to libcurl lets
     * its TCP, TLS, and WebSocket Upgrade state machine continue running.
     */
    result = curl_multi_socket_action(
        event_loop->multi,
        event_loop->websocket_socket,
        action,
        &running_handles
    );

    if (result != CURLM_OK) {
        logf_error(
            "failed to advance WebSocket socket: %s",
            curl_multi_strerror(result)
        );
        schedule_cleanup_websocket(event_loop);
        return;
    }

    check_and_perform_socket_state_machine(event_loop);
}

static void websocket_timeout(uv_timer_t *timer) {
    EventLoop *event_loop = timer->data;
    int running_handles = 0;
    CURLMcode result;

    /* The requested deadline has arrived, so libcurl can continue running. */
    result = curl_multi_socket_action(
        event_loop->multi,
        CURL_SOCKET_TIMEOUT,
        0,
        &running_handles
    );

    if (result != CURLM_OK) {
        logf_error(
            "failed to advance WebSocket timeout: %s",
            curl_multi_strerror(result)
        );
        schedule_cleanup_websocket(event_loop);
        return;
    }

    check_and_perform_socket_state_machine(event_loop);
}

int curl_timer_status_updated(
    CURLM *multi,
    long timeout_ms,
    void *context
) {
    EventLoop *event_loop = context;
    int result;

    (void)multi;

    if (timeout_ms < 0) {
        result = uv_timer_stop(&event_loop->timer);
    } else {
        /* A new libcurl deadline always replaces the previous one. */
        result = uv_timer_start(
            &event_loop->timer,
            websocket_timeout,
            (uint64_t)timeout_ms,
            0
        );
    }

    if (result != 0) {
        logf_error(
            "failed to update WebSocket timeout: %s",
            uv_strerror(result)
        );
        return -1;
    }

    return 0;
}

int curl_socket_status_updated(
    CURL *easy,
    curl_socket_t socket,
    int action,
    void *context,
    void *socket_context
) {
    EventLoop *event_loop = context;
    int events;
    int result;

    (void)easy;

    /* REMOVE can arrive after cleanup has already cleared socket_context. */
    if (action == CURL_POLL_REMOVE) {
        if (socket_context != NULL) {
            uv_poll_stop(&event_loop->websocket_poll);
        }

        return 0;
    }

    /*
     * This EventLoop owns one WebSocket and therefore one curl socket.
     * socket_context is only used to identify the first notification.
     */
    if (socket_context == NULL) {
        event_loop->websocket_socket = socket;
        result = uv_poll_init_socket(
            &event_loop->loop,
            &event_loop->websocket_poll,
            socket
        );

        if (result != 0) {
            logf_error(
                "failed to initialize WebSocket socket: %s",
                uv_strerror(result)
            );
            return -1;
        }

        event_loop->websocket_poll.data = event_loop;

        if (curl_multi_assign(
                event_loop->multi,
                socket,
                &event_loop->websocket_poll
            ) != CURLM_OK) {
            uv_close((uv_handle_t *)&event_loop->websocket_poll, NULL);
            return -1;
        }
    }

    if (action == CURL_POLL_IN) {
        events = UV_READABLE;
    } else if (action == CURL_POLL_OUT) {
        events = UV_WRITABLE;
    } else {
        events = UV_READABLE | UV_WRITABLE;
    }

    result = uv_poll_start(
        &event_loop->websocket_poll,
        events,
        websocket_socket_event
    );

    if (result != 0) {
        logf_error(
            "failed to watch WebSocket socket: %s",
            uv_strerror(result)
        );
        return -1;
    }

    return 0;
}
