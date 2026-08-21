#include "event_loop.h"

#include "api_base.h"
#include "logger.h"
#include "websocket_eventloop.h"
#include "websocket_send.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char remotify_ca_bundle[];
extern const size_t remotify_ca_bundle_length;

static void schedule_connect_remotify(uv_handle_t *handle);

static bool init_websocket(EventLoop *event_loop, const char *credential) {
    CURL *websocket = curl_easy_init();
    struct curl_slist *headers;
    char *authorization;
    const struct curl_blob certificates = {
        .data = (void *)remotify_ca_bundle,
        .len = remotify_ca_bundle_length,
        .flags = CURL_BLOB_NOCOPY
    };

    if (websocket == NULL) {
        return false;
    }

    authorization = malloc(sizeof("Authorization: Bearer ") + strlen(credential));
    sprintf(authorization, "Authorization: Bearer %s", credential);
    headers = curl_slist_append(NULL, authorization);

    /* curl_slist_append copied the complete header into its own allocation. */
    free(authorization);

    if (headers == NULL) {
        curl_easy_cleanup(websocket);
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
            CURLOPT_URL,
            get_websocket_url()
        ) != CURLE_OK ||
        curl_easy_setopt(
            websocket,
            CURLOPT_HTTPHEADER,
            headers
        ) != CURLE_OK ||
        curl_easy_setopt(
            websocket,
            CURLOPT_PRIVATE,
            headers
        ) != CURLE_OK ||
        curl_easy_setopt(
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
        curl_slist_free_all(headers);
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
    struct curl_slist *headers = NULL;

    if (websocket == NULL) {
        return;
    }

    /* No queued write may run while this connection is being destroyed. */
    uv_timer_stop(&event_loop->websocket_send_timer);

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

    /* CURLOPT_PRIVATE keeps the header list paired with its easy handle. */
    curl_easy_getinfo(websocket, CURLINFO_PRIVATE, &headers);
    curl_easy_cleanup(websocket);
    curl_slist_free_all(headers);
    event_loop->websocket = NULL;
    event_loop->websocket_message_length = 0;

    /* Reconnect only after libuv has completely released its socket watcher. */
    cleanup_websocket_resource(
        event_loop,
        schedule_connect_remotify
    );
}

static void schedule_connect_remotify(uv_handle_t *handle) {
    EventLoop *event_loop = handle->data;
    int result;

    /* Frames owned by the previous connection are never sent after reconnect. */
    websocket_send_clear(event_loop);

    /* libuv has finished closing the handle, so its storage can now be reset. */
    event_loop->websocket_poll = (uv_poll_t){0};

    result = uv_timer_start(
        &event_loop->timer,
        connect_remotify,
        5000,
        0
    );

    if (result != 0) {
        /*
         * The process-owned timer remains valid for the EventLoop lifetime,
         * so scheduling this fixed one-shot reconnect cannot normally fail.
         */
        logf_error(
            "failed to schedule WebSocket reconnect: %s",
            uv_strerror(result)
        );
    }
}

void schedule_cleanup_websocket(EventLoop *event_loop) {
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

void connect_remotify(uv_timer_t *timer) {
    EventLoop *event_loop = timer->data;
    int running_handles = 0;
    CURLMcode result;

    if (!init_websocket(event_loop, event_loop->state.token)) {
        logf_error("failed to initialize WebSocket");
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
    result = curl_multi_socket_action(
        event_loop->multi,
        CURL_SOCKET_TIMEOUT,
        0,
        &running_handles
    );

    if (result != CURLM_OK) {
        /* Keep easy-handle destruction outside the multi-action path. */
        schedule_cleanup_websocket(event_loop);
        return;
    }

    check_and_perform_socket_state_machine(event_loop);
}

int event_loop_run(EventLoop *event_loop) {
    return uv_run(&event_loop->loop, UV_RUN_DEFAULT);
}
