#ifndef REMOTIFY_EXECUTOR_H
#define REMOTIFY_EXECUTOR_H

#include "event_loop.h"

#include <stdbool.h>

bool launch_executor(EventLoop *event_loop, const char *executable);

bool dispatch_request_to_executor(
    EventLoop *event_loop,
    const IpcHeader *request,
    const uint8_t *body
);

#endif
