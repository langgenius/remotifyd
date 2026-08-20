#ifndef REMOTIFY_WEBSOCKET_EVENTLOOP_H
#define REMOTIFY_WEBSOCKET_EVENTLOOP_H

#include "event_loop.h"

#include <curl/curl.h>

int curl_socket_status_updated(
    CURL *easy,
    curl_socket_t socket,
    int action,
    void *context,
    void *socket_context
);

int curl_timer_status_updated(
    CURLM *multi,
    long timeout_ms,
    void *context
);

void check_and_perform_socket_state_machine(EventLoop *event_loop);
void cleanup_websocket_resource(
    EventLoop *event_loop,
    uv_close_cb after_cleaned_cb
);

#endif
