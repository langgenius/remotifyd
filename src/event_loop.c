#include "event_loop.h"

#include "logger.h"

#include <stdbool.h>
#include <stddef.h>

extern const unsigned char remotify_ca_bundle[];
extern const size_t remotify_ca_bundle_length;

static bool init_websocket(EventLoop *event_loop) {
    CURL *websocket = curl_easy_init();
    const struct curl_blob certificates = {
        .data = (void *)remotify_ca_bundle,
        .len = remotify_ca_bundle_length,
        .flags = CURL_BLOB_NOCOPY
    };

    if (websocket == NULL) {
        return false;
    }

    /*
     * The embedded bundle gives old and current operating systems the same
     * trust store. Its storage lives for the process lifetime, which satisfies
     * CURL_BLOB_NOCOPY without allocating another certificate buffer.
     *
     * CONNECT_ONLY performs the HTTP Upgrade but leaves WebSocket frame I/O to
     * this event loop. The timeout applies only while establishing the socket;
     * a connected daemon must not be ended by a total-transfer timeout.
     */
    if (curl_easy_setopt(
            websocket,
            CURLOPT_CAINFO,
            NULL
        ) != CURLE_OK ||
        curl_easy_setopt(
            websocket,
            CURLOPT_CAINFO_BLOB,
            &certificates
        ) != CURLE_OK ||
        curl_easy_setopt(
            websocket,
            CURLOPT_NOSIGNAL,
            1L
        ) != CURLE_OK ||
        curl_easy_setopt(
            websocket,
            CURLOPT_CONNECT_ONLY,
            2L
        ) != CURLE_OK ||
        curl_easy_setopt(
            websocket,
            CURLOPT_CONNECTTIMEOUT,
            30L
        ) != CURLE_OK) {
        curl_easy_cleanup(websocket);
        return false;
    }

    /*
     * Publish the handle only after initialization succeeds, so EventLoop
     * never contains a partially initialized WebSocket handle.
     */
    event_loop->websocket = websocket;
    return true;
}

static void cleanup_websocket(uv_timer_t *timer) {
    EventLoop *event_loop = timer->data;
    CURL *websocket = event_loop->websocket;

    if (websocket == NULL) {
        return;
    }

    /*
     * An easy handle must leave the multi handle before it is destroyed.
     * Keeping that order lets the long-lived multi handle release all state
     * belonging to this connection before the WebSocket handle disappears.
     */
    if (event_loop->multi != NULL) {
        curl_multi_remove_handle(
            event_loop->multi,
            websocket
        );
    }

    curl_easy_cleanup(websocket);
    event_loop->websocket = NULL;
}

static void schedule_cleanup_websocket(EventLoop *event_loop) {
    int result;

    /*
     * A zero-delay one-shot timer establishes one cleanup boundary for every
     * failed connection path. Code that adds or advances an easy handle never
     * destroys that handle in the same event-loop turn.
     */
    result = uv_timer_start(
        &event_loop->timer,
        cleanup_websocket,
        0,
        0
    );

    if (result != 0) {
        /*
         * main initialized this timer successfully and this call uses a fixed
         * valid callback with zero timeout and zero repeat, so failure is not
         * expected during a valid process lifetime. Log the violated libuv
         * invariant; do not introduce a fallback cleanup path.
         */
        logf_error(
            "failed to schedule WebSocket cleanup: %s",
            uv_strerror(result)
        );
    }
}

static void remotify_connect(EventLoop *event_loop) {
    int running_handles = 0;

    if (!init_websocket(event_loop)) {
        return;
    }

    if (curl_multi_add_handle(
            event_loop->multi,
            event_loop->websocket
        ) != CURLM_OK) {
        /* Keep easy-handle destruction outside the connection attempt. */
        schedule_cleanup_websocket(event_loop);
        return;
    }

    /*
     * No socket event exists before the first multi action. Driving the
     * timeout socket once lets libcurl begin the connection and publish the
     * sockets that the uv loop will subsequently watch.
     */
    if (curl_multi_socket_action(
            event_loop->multi,
            CURL_SOCKET_TIMEOUT,
            0,
            &running_handles
        ) != CURLM_OK) {
        /* Keep easy-handle destruction outside the multi-action path. */
        schedule_cleanup_websocket(event_loop);
    }
}

int event_loop_run(EventLoop *event_loop) {
    /*
     * The multi handle must exist before the loop can run any WebSocket work.
     * It then remains alive across every individual WebSocket connection.
     */
    if (!init_curl_multi(event_loop)) {
        return 1;
    }

    return uv_run(&event_loop->loop, UV_RUN_DEFAULT);
}
