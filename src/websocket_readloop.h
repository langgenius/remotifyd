#ifndef REMOTIFY_WEBSOCKET_READLOOP_H
#define REMOTIFY_WEBSOCKET_READLOOP_H

#include <uv.h>

void websocket_read(
    uv_poll_t *poll,
    int status,
    int events
);

#endif
