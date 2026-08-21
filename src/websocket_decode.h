#ifndef REMOTIFY_WEBSOCKET_DECODE_H
#define REMOTIFY_WEBSOCKET_DECODE_H

#include "event_loop.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool websocket_decode(
    EventLoop *event_loop,
    const uint8_t *message,
    size_t message_length
);

#endif
