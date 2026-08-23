#include "event_loop.h"

#include "logger.h"

#include <stdlib.h>
#include <string.h>

enum { EVENT_LOOP_EXECUTOR_READ_BYTES = 64 * 1024 };

void event_loop_executor_output_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer) {
    (void)handle;
    (void)suggested_size;

    /*
     * One bounded allocation belongs to one uv read callback. The matching
     * output callback releases it after consuming or rejecting those bytes.
     */
    buffer->base = malloc(EVENT_LOOP_EXECUTOR_READ_BYTES);
    buffer->len = buffer->base == NULL ? 0 : EVENT_LOOP_EXECUTOR_READ_BYTES;
}

void event_loop_executor_output_read(uv_stream_t *stream, ssize_t received, const uv_buf_t *buffer) {
    EventLoop *event_loop = stream->data;
    IpcReadBuffer *read = &event_loop->executor_read;
    IpcProcessCallback callback;
    size_t cursor = 0;

    if (received == UV_EOF) {
        logf_info("Executor output closed");
        uv_read_stop(stream);
        free(buffer->base);
        return;
    }

    if (received < 0) {
        logf_error("failed to read Executor output: %s", uv_strerror((int)received));
        uv_read_stop(stream);
        free(buffer->base);
        return;
    }

    /*
     * This is the parent side of the same buffer-driven frame state machine
     * used by ExecutorWorker. cursor describes only this callback's libuv
     * buffer; executor_read keeps an incomplete response across callbacks.
     *
     * Each iteration performs exactly one state transition:
     *
     *   1. Copy bytes into the fixed-size header, then continue.
     *   2. Once the header is complete, copy bytes into the declared body,
     *      then continue.
     *   3. Once both are complete, handle the response and reset the state;
     *      remaining bytes in this libuv buffer begin the next response.
     *
     * The completed-frame loop condition handles a response even when its
     * final body byte is also the final byte supplied by this callback.
     */
    while (
        cursor < (size_t)received ||
        (
            read->header_received == sizeof(IpcHeader) &&
            read->body_received == read->header.body_length
        )
    ) {
        size_t remaining = (size_t)received - cursor;

        if (read->header_received < sizeof(IpcHeader)) {
            size_t needed = sizeof(IpcHeader) - read->header_received;
            size_t copied = remaining < needed ? remaining : needed;

            memcpy((uint8_t *)&read->header + read->header_received, buffer->base + cursor, copied);

            read->header_received += copied;
            cursor += copied;
            continue;
        }

        if (read->body_received < read->header.body_length) {
            size_t needed = read->header.body_length - read->body_received;
            size_t copied = remaining < needed ? remaining : needed;

            if (read->body == NULL) {
                read->body = malloc(read->header.body_length);

                if (read->body == NULL) {
                    logf_error("failed to allocate Executor output body");
                    uv_read_stop(stream);
                    break;
                }
            }

            memcpy(read->body + read->body_received, buffer->base + cursor, copied);

            read->body_received += copied;
            cursor += copied;
            continue;
        }

        if (read->body_received == read->header.body_length) {
            callback = ipc_process_callbacks_get(
                &event_loop->callbacks,
                uv_now(&event_loop->loop),
                (uint32_t)read->header.request_id
            );

            if (callback != NULL) {
                callback(
                    event_loop,
                    (uint32_t)read->header.request_id,
                    &read->header,
                    read->body
                );
            }

            read->header_received = 0;
            read->body_received = 0;

            free(read->body);
            read->body = NULL;
        }
    }

    free(buffer->base);
}
