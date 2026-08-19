#include "executor_worker.h"

#include "logger.h"

#include <stdlib.h>
#include <string.h>

static void executor_worker_output_written(uv_write_t *write, int status) {
    if (status < 0) {
        logf_error("failed to write Executor IPC frame: %s", uv_strerror(status));
    }

    /*
     * Reaching this callback means libuv has finished using both byte ranges.
     * data points to the body copy; the header copy lives directly after the
     * uv_write_t allocation, so freeing write releases both write and header.
     */
    free(write->data);
    free(write);
}

bool executor_worker_output(ExecutorWorker *worker, const IpcHeader *header, const uint8_t *body) {
    uv_write_t *write;
    IpcHeader *header_bytes;
    uint8_t *body_bytes = NULL;
    uv_buf_t buffers[2];
    unsigned int buffer_count;
    int result;

    /*
     * uv_write returns before the operating system necessarily finishes the
     * write, so uv_write_t cannot be a local variable. The same allocation
     * also stores a private header copy immediately after uv_write_t.
     *
     * This is not memory-efficient: the extra allocations and memcpy are a
     * temporary solution. They deliberately make ownership obvious enough
     * for AI-generated changes to preserve the asynchronous memory lifetime
     * correctly instead of introducing widespread leaks or use-after-free
     * bugs. The copies can be removed later behind an equally explicit
     * ownership rule.
     */
    write = malloc(sizeof(*write) + sizeof(*header));

    if (write == NULL) {
        logf_error("failed to allocate Executor IPC write");
        return false;
    }

    /* The caller may reuse its header as soon as this function returns. */
    header_bytes = (IpcHeader *)(write + 1);
    *header_bytes = *header;

    if (header->body_length > 0) {
        /* The body must remain valid until the asynchronous write completes. */
        body_bytes = malloc(header->body_length);

        if (body_bytes == NULL) {
            logf_error("failed to allocate Executor IPC body write");
            free(write);
            return false;
        }

        memcpy(body_bytes, body, header->body_length);
    }

    /*
     * uv_buf_init only describes an address and a length; it neither allocates
     * nor copies bytes. Header and body remain two separate byte ranges.
     */
    buffers[0] = uv_buf_init((char *)header_bytes, sizeof(*header_bytes));
    buffers[1] = uv_buf_init((char *)body_bytes, header->body_length);

    /* The completion callback retrieves the owned body through this pointer. */
    write->data = body_bytes;
    buffer_count = header->body_length == 0 ? 1 : 2;

    /*
     * One uv_write queues the header and body together and preserves their
     * order. libuv copies the uv_buf_t descriptions, but uses the underlying
     * header_bytes and body_bytes until the completion callback runs.
     */
    result = uv_write(write, (uv_stream_t *)&worker->output, buffers, buffer_count, executor_worker_output_written);

    if (result != 0) {
        /* No callback follows a failed submission, so ownership stays here. */
        logf_error("failed to queue Executor IPC frame: %s", uv_strerror(result));
        free(body_bytes);
        free(write);
        return false;
    }

    return true;
}
