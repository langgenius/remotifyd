#ifndef REMOTIFY_EVENT_LOOP_H
#define REMOTIFY_EVENT_LOOP_H

#include "ipc_types.h"

#include <stdbool.h>
#include <curl/curl.h>
#include <stdint.h>
#include <uv.h>

/*
 * EventLoop is allocated by main and remains alive until uv_run returns.
 * Every asynchronous handle can therefore point back to this one owner
 * without introducing globals or separately allocated callback contexts.
 */
typedef struct EventLoop {
    uv_loop_t loop;

    /*
     * The single timer separates libcurl state advancement from WebSocket
     * destruction by one event-loop turn. Reusing one handle avoids a timer
     * allocation for each failed connection attempt.
     */
    uv_timer_t timer;

    uv_process_t executor;
    uv_pipe_t executor_input;
    uv_pipe_t executor_output;
    IpcReadBuffer executor_read;

    IpcProcessCallbacks callbacks;

    CURLM *multi;
    CURL *websocket;
    uint64_t next_request_id;
} EventLoop;

int event_loop_run(EventLoop *event_loop);

bool init_curl_multi(EventLoop *event_loop);

void event_loop_executor_output_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer);
void event_loop_executor_output_read(uv_stream_t *stream, ssize_t received, const uv_buf_t *buffer);
void event_loop_executor_handle(EventLoop *event_loop, const IpcHeader *header, const uint8_t *body);

bool launch_executor(
    EventLoop *event_loop,
    const char *executable
);

#endif
