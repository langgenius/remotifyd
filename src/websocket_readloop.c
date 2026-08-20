#include "websocket_readloop.h"

void websocket_read(
    uv_poll_t *poll,
    int status,
    int events
) {
    /* WebSocket receive and decode are implemented in the next step. */
    (void)poll;
    (void)status;
    (void)events;
}
