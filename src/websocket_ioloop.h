#ifndef REMOTIFY_WEBSOCKET_IOLOOP_H
#define REMOTIFY_WEBSOCKET_IOLOOP_H

#include "event_loop.h"

#include <uv.h>

void websocket_socket_event(uv_poll_t *poll, int status, int events);
void websocket_write(uv_timer_t *timer);

#endif
