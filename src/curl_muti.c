#include "event_loop.h"

bool init_curl_multi(EventLoop *event_loop) {
    CURLM *multi = curl_multi_init();

    if (multi == NULL) {
        return false;
    }

    /*
     * The multi handle belongs to the parent event loop and survives WebSocket
     * reconnects. Individual WebSocket easy handles are added and removed;
     * the multi handle is cleaned up only when the parent process exits.
     */
    event_loop->multi = multi;
    return true;
}
