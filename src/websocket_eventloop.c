#include "websocket_eventloop.h"

#include "event_loop.h"
#include "logger.h"
#include "websocket_ioloop.h"
#include "websocket_send.h"

#include <stdlib.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

static CurlSocketWatch *find_socket_watch(
    EventLoop *event_loop,
    curl_socket_t socket
) {
    CurlSocketWatch *watch;

    for (watch = event_loop->curl_socket_watches;
         watch != NULL;
         watch = watch->next) {
        if (watch->socket == socket) return watch;
    }

    return NULL;
}

static void socket_watch_closed(uv_handle_t *handle) {
    CurlSocketWatch *watch = handle->data;
    EventLoop *event_loop = watch->event_loop;
    CurlSocketWatch **slot = &event_loop->curl_socket_watches;
    uv_close_cb after_closed;

    while (*slot != watch) slot = &(*slot)->next;
    *slot = watch->next;
    free(watch);

    if (event_loop->curl_socket_watches != NULL ||
        event_loop->socket_watches_closed == NULL) return;

    after_closed = event_loop->socket_watches_closed;
    event_loop->socket_watches_closed = NULL;
    after_closed((uv_handle_t *)&event_loop->timer);
}

static void close_socket_watch(CurlSocketWatch *watch) {
    if (uv_is_closing((uv_handle_t *)&watch->poll)) return;

    uv_poll_stop(&watch->poll);
    uv_close((uv_handle_t *)&watch->poll, socket_watch_closed);
}

static void close_socket_watches_except(
    EventLoop *event_loop,
    CurlSocketWatch *keep
) {
    CurlSocketWatch *watch;

    for (watch = event_loop->curl_socket_watches;
         watch != NULL;
         watch = watch->next)
        if (watch != keep) close_socket_watch(watch);
}

void close_websocket_socket(EventLoop *event_loop) {
    /*
     * shutdown leaves the descriptor owned by libcurl, but makes the read side
     * observe the connection end. Resource destruction remains in the single
     * cleanup path reached by websocket_read.
     */
#if defined(_WIN32)
    shutdown(event_loop->websocket_watch->socket, SD_BOTH);
#else
    shutdown(event_loop->websocket_watch->socket, SHUT_RDWR);
#endif
}

void cleanup_websocket_resource(
    EventLoop *event_loop,
    uv_close_cb after_cleaned_cb
) {
    event_loop->websocket_watch = NULL;
    event_loop->socket_watches_closed = after_cleaned_cb;
    close_socket_watches_except(event_loop, NULL);

    if (event_loop->curl_socket_watches == NULL) {
        event_loop->socket_watches_closed = NULL;
        after_cleaned_cb((uv_handle_t *)&event_loop->timer);
    }
}

void stop_websocket_resources(EventLoop *event_loop) {
    CurlSocketWatch *watch;

    for (watch = event_loop->curl_socket_watches;
         watch != NULL;
         watch = watch->next)
        if (!uv_is_closing((uv_handle_t *)&watch->poll))
            uv_poll_stop(&watch->poll);
}

static void liveness_check(uv_timer_t *timer) {
    EventLoop *event_loop = timer->data;
    uint64_t now = uv_now(&event_loop->loop);

    /*
     * A half-open connection is silent: writes may still be accepted by the
     * local stack while the peer is unreachable. Without a receive deadline
     * the daemon keeps an ESTABLISHED socket forever and the backend drops
     * the device from presence. Treat 45 seconds without any frame as dead.
     */
    if (now - event_loop->last_rx_ms > 45000) {
        uv_timer_stop(timer);
        logf_error("WebSocket receive deadline expired; reconnecting");
        schedule_cleanup_websocket(event_loop);
    }
}

void check_websocket_connect_result(EventLoop *event_loop) {
    CURLMsg *message;
    CurlSocketWatch *watch;
    curl_socket_t active_socket;
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

        if (curl_easy_getinfo(
                event_loop->websocket,
                CURLINFO_ACTIVESOCKET,
                &active_socket
            ) != CURLE_OK ||
            active_socket == CURL_SOCKET_BAD ||
            (watch = find_socket_watch(event_loop, active_socket)) == NULL) {
            logf_error("failed to identify active WebSocket socket");
            schedule_cleanup_websocket(event_loop);
            return;
        }

        /* Only libcurl's selected socket survives as the WebSocket watcher. */
        event_loop->websocket_watch = watch;

        /* The upgrade completed: this attempt no longer needs its deadline. */
        uv_timer_stop(&event_loop->handshake_timer);

        result = uv_poll_start(
            &watch->poll,
            UV_READABLE | UV_DISCONNECT,
            websocket_socket_event
        );

        if (result != 0) {
            logf_error(
                "failed to start WebSocket readloop: %s",
                uv_strerror(result)
            );
            schedule_cleanup_websocket(event_loop);
            return;
        }

        close_socket_watches_except(event_loop, watch);

        event_loop->last_rx_ms = uv_now(&event_loop->loop);
        result = uv_timer_start(
            &event_loop->liveness_timer,
            liveness_check,
            10000,
            10000
        );

        if (result != 0) {
            logf_error(
                "failed to start WebSocket liveness timer: %s",
                uv_strerror(result)
            );
        }

        /*
         * Heartbeat is an application request that refreshes the authenticated
         * device route. It uses the same ordered asynchronous queue as every
         * other WebSocket message and never writes the socket directly.
         */
        result = uv_timer_start(
            &event_loop->heartbeat_timer,
            websocket_send_heartbeat,
            15000,
            15000
        );

        if (result != 0) {
            /* This process-owned timer remains valid for EventLoop's lifetime. */
            logf_error(
                "failed to start Heartbeat timer: %s",
                uv_strerror(result)
            );
        }

        return;
    }
}

/*
 * Before WebSocket Upgrade completes, readiness on the connection socket
 * tells libcurl that TCP or TLS can continue.
 */
static void curl_socket_ready(
    uv_poll_t *poll,
    int status,
    int events
) {
    CurlSocketWatch *watch = poll->data;
    EventLoop *event_loop = watch->event_loop;
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
        watch->socket,
        action,
        &running_handles
    );

    if (result != CURLM_OK) {
        logf_error(
            "failed to advance curl socket: %s",
            curl_multi_strerror(result)
        );
        schedule_cleanup_websocket(event_loop);
        return;
    }

    check_websocket_connect_result(event_loop);
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

    check_websocket_connect_result(event_loop);
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

int curl_socket_watch_requested(
    CURL *easy,
    curl_socket_t socket,
    int action,
    void *context,
    void *socket_context
) {
    EventLoop *event_loop = context;
    CurlSocketWatch *watch = socket_context;
    int events;
    int result;

    (void)easy;

    /* libcurl owns the descriptor; REMOVE only ends our readiness interest. */
    if (action == CURL_POLL_REMOVE) {
        if (watch != NULL &&
            !uv_is_closing((uv_handle_t *)&watch->poll))
            uv_poll_stop(&watch->poll);

        return 0;
    }

    if (watch == NULL) {
        watch = calloc(1, sizeof(*watch));
        if (watch == NULL) return -1;

        watch->event_loop = event_loop;
        watch->socket = socket;
        result = uv_poll_init_socket(
            &event_loop->loop,
            &watch->poll,
            socket
        );

        if (result != 0) {
            free(watch);
            logf_error(
                "failed to initialize curl socket watch: %s",
                uv_strerror(result)
            );
            return -1;
        }

        watch->poll.data = watch;
        watch->next = event_loop->curl_socket_watches;
        event_loop->curl_socket_watches = watch;

        if (curl_multi_assign(
                event_loop->multi,
                socket,
                watch
            ) != CURLM_OK) {
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
        &watch->poll,
        events,
        curl_socket_ready
    );

    if (result != 0) {
        logf_error(
            "failed to watch curl socket: %s",
            uv_strerror(result)
        );
        return -1;
    }

    return 0;
}
