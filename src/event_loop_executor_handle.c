#include "event_loop.h"

/*
 * The read loop calls this function only after one complete Executor response
 * has arrived. Callback lookup and response handling belong here so pipe
 * buffering remains independent from the request that produced the response.
 */
void event_loop_executor_handle(
    EventLoop *event_loop,
    const IpcHeader *header,
    const uint8_t *body
) {
    (void)event_loop;
    (void)header;
    (void)body;
}
