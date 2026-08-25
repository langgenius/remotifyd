#include "curl_multi.h"

#include "websocket_eventloop.h"

bool init_curl_multi(EventLoop *event_loop) {
    CURLM *multi = curl_multi_init();

    if (multi == NULL) return false;

    if (curl_multi_setopt(
            multi,
            CURLMOPT_SOCKETFUNCTION,
            curl_socket_watch_requested
        ) != CURLM_OK ||
        curl_multi_setopt(
            multi,
            CURLMOPT_SOCKETDATA,
            event_loop
        ) != CURLM_OK ||
        curl_multi_setopt(
            multi,
            CURLMOPT_TIMERFUNCTION,
            curl_timer_status_updated
        ) != CURLM_OK ||
        curl_multi_setopt(
            multi,
            CURLMOPT_TIMERDATA,
            event_loop
        ) != CURLM_OK) {
        curl_multi_cleanup(multi);
        return false;
    }

    /* One multi handle serves every WebSocket connection on this EventLoop. */
    event_loop->multi = multi;
    return true;
}
