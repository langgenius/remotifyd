#include "executor_worker.h"

#include "logger.h"

#include <stdlib.h>
#include <string.h>

enum { EXECUTOR_WORKER_READ_BYTES = 64 * 1024 };

void executor_worker_input_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer) {
    (void)handle;
    (void)suggested_size;

    /*
     * One bounded allocation belongs to one uv read callback. The matching
     * input callback releases it after consuming or rejecting those bytes.
     */
    buffer->base = malloc(EXECUTOR_WORKER_READ_BYTES);
    buffer->len = buffer->base == NULL ? 0 : EXECUTOR_WORKER_READ_BYTES;
}

void executor_worker_input_read(uv_stream_t *stream, ssize_t received, const uv_buf_t *buffer) {
    ExecutorWorker *worker = stream->data;
    IpcReadBuffer *read = &worker->input_read;
    size_t cursor = 0;

    if (received == UV_EOF) {
        logf_info("Executor parent input closed");
        uv_read_stop(stream);
        free(buffer->base);
        return;
    }

    if (received < 0) {
        logf_error("failed to read Executor parent input: %s", uv_strerror((int)received));
        uv_read_stop(stream);
        free(buffer->base);
        return;
    }

    /*
     * This is a buffer-driven frame state machine. cursor belongs only to the
     * bytes delivered by this callback, while header_received, body_received,
     * and body remain in ExecutorWorker so an incomplete frame can continue
     * in the next callback.
     *
     * Each iteration performs exactly one state transition:
     *
     *   1. Copy bytes into the fixed-size header, then continue.
     *   2. Once the header is complete, copy bytes into the declared body,
     *      then continue.
     *   3. Once both are complete, handle the frame and reset the read state;
     *      any bytes left in this libuv buffer begin the next frame.
     *
     * Keeping these transitions separate means header parsing, body parsing,
     * and frame dispatch never share one branch. The second loop condition is
     * required because a body can become complete exactly when cursor reaches
     * received; that completed frame must still be handled in this callback.
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
                    logf_error("failed to allocate Executor IPC body");
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
            executor_worker_handle(worker, &read->header, read->body);

            read->header_received = 0;
            read->body_received = 0;

            free(read->body);
            read->body = NULL;
        }
    }

    free(buffer->base);
}
