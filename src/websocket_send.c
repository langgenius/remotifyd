#include "websocket_send.h"

#include "circular_queue.h"
#include "device_protocol_gen.h"
#include "logger.h"
#include "websocket_ioloop.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* curl headers can expose stdio's macros, which collide with RPC field names. */
#ifdef stdout
#undef stdout
#endif

#ifdef stderr
#undef stderr
#endif

enum {
    /* Leave room for the fixed frame header and every ExecReport field. */
    WEBSOCKET_EXEC_OUTPUT_BYTES = DEVICE_MAXIMUM_FRAME_BYTES - 96
};

CURLcode websocket_send(EventLoop *event_loop, WebSocketSend *send) {
    size_t sent = 0;
    CURLcode result = curl_ws_send(
        event_loop->websocket,
        send->bytes + send->sent,
        send->length - send->sent,
        &sent,
        0,
        CURLWS_BINARY
    );

    send->sent += sent;
    return result;
}

static void websocket_bytes_consumed(
    WebSocketSend *send,
    bool succeed
) {
    (void)succeed;
    free(send->bytes);
}

static bool websocket_send_put(EventLoop *event_loop, WebSocketSend *send) {
    CircularQueue *queue = &event_loop->websocket_send_queue;
    bool was_empty = queue->length == 0;
    int result;

    if (!circular_queue_put(queue, send)) return false;
    if (!was_empty) return true;

    /* The first queued message starts the one-shot asynchronous writer. */
    result = uv_timer_start(&event_loop->websocket_send_timer, websocket_write, 0, 0);

    if (result != 0) {
        /*
         * This process-owned timer is initialized once and never closed while
         * EventLoop is running, so this call cannot normally fail. Recording
         * the violated invariant is preferable to adding another send path.
         */
        logf_error(
            "failed to schedule WebSocket write: %s",
            uv_strerror(result)
        );
    }

    return true;
}

static bool websocket_send_exec_report_frame(
    EventLoop *event_loop,
    uint32_t request_id,
    const IpcExecReportHeader *report,
    const uint8_t *local_process_id,
    size_t local_process_id_length,
    const uint8_t *stdout_bytes,
    size_t stdout_length,
    const uint8_t *stderr_bytes,
    size_t stderr_length,
    bool ended
) {
    DeviceExecReportV1 message = {
        .operation_id = (const uint8_t *)report->operation_id,
        .operation_id_length = IPC_OPERATION_ID_LENGTH,
        .local_process_id = local_process_id,
        .local_process_id_length = local_process_id_length,
        .stdout = stdout_bytes,
        .stdout_length = stdout_length,
        .stderr = stderr_bytes,
        .stderr_length = stderr_length,
        .has_exit_code = ended,
        .exit_code = report->exit_code
    };
    WebSocketSend *send = malloc(sizeof(*send));

    if (send == NULL) return false;

    send->bytes = malloc(DEVICE_MAXIMUM_FRAME_BYTES);

    if (send->bytes == NULL) {
        free(send);
        return false;
    }

    send->sent = 0;
    send->consumed_callback = websocket_bytes_consumed;

    /* Encode directly into the owned send buffer; no frame copy is needed. */
    if (!device_encode_exec_report_v1(
            send->bytes,
            DEVICE_MAXIMUM_FRAME_BYTES,
            &send->length,
            request_id,
            &message
        )) {
        send->consumed_callback(send, false);
        free(send);
        return false;
    }

    if (!websocket_send_put(event_loop, send)) {
        send->consumed_callback(send, false);
        free(send);
        return false;
    }

    return true;
}

bool websocket_send_exec_report(
    EventLoop *event_loop,
    uint32_t request_id,
    const IpcExecReportHeader *report,
    bool ended
) {
    const uint8_t *stdout_bytes = (const uint8_t *)(report + 1);
    const uint8_t *stderr_bytes = stdout_bytes + report->stdout_length;
    size_t stdout_remaining = report->stdout_length;
    size_t stderr_remaining = report->stderr_length;
    char local_process_id[24];
    const uint8_t *local_process_id_bytes = NULL;
    size_t local_process_id_length = 0;
    bool first = true;

    if (report->local_process_id != 0) {
        local_process_id_length = (size_t)sprintf(
            local_process_id,
            "%llu",
            (unsigned long long)report->local_process_id
        );
        local_process_id_bytes = (const uint8_t *)local_process_id;
    }

    if (stdout_remaining == 0 && stderr_remaining == 0) {
        return websocket_send_exec_report_frame(
            event_loop,
            request_id,
            report,
            local_process_id_bytes,
            local_process_id_length,
            NULL,
            0,
            NULL,
            0,
            ended
        );
    }

    while (stdout_remaining != 0) {
        size_t length = stdout_remaining < WEBSOCKET_EXEC_OUTPUT_BYTES
            ? stdout_remaining
            : WEBSOCKET_EXEC_OUTPUT_BYTES;
        bool frame_ended = ended && length == stdout_remaining && stderr_remaining == 0;

        if (!websocket_send_exec_report_frame(
                event_loop,
                request_id,
                report,
                first ? local_process_id_bytes : NULL,
                first ? local_process_id_length : 0,
                stdout_bytes,
                length,
                NULL,
                0,
                frame_ended
            )) {
            return false;
        }

        first = false;
        stdout_bytes += length;
        stdout_remaining -= length;
    }

    while (stderr_remaining != 0) {
        size_t length = stderr_remaining < WEBSOCKET_EXEC_OUTPUT_BYTES
            ? stderr_remaining
            : WEBSOCKET_EXEC_OUTPUT_BYTES;
        bool frame_ended = ended && length == stderr_remaining;

        if (!websocket_send_exec_report_frame(
                event_loop,
                request_id,
                report,
                first ? local_process_id_bytes : NULL,
                first ? local_process_id_length : 0,
                NULL,
                0,
                stderr_bytes,
                length,
                frame_ended
            )) {
            return false;
        }

        first = false;
        stderr_bytes += length;
        stderr_remaining -= length;
    }

    return true;
}

bool websocket_send_exec_failure(
    EventLoop *event_loop,
    uint32_t request_id,
    const uint8_t *diagnostic,
    size_t diagnostic_length
) {
    WebSocketSend *send = malloc(sizeof(*send));

    if (send == NULL) return false;

    send->bytes = malloc(DEVICE_MAXIMUM_FRAME_BYTES);

    if (send->bytes == NULL) {
        free(send);
        return false;
    }

    send->sent = 0;
    send->consumed_callback = websocket_bytes_consumed;

    if (!device_encode_failure(
            send->bytes,
            DEVICE_MAXIMUM_FRAME_BYTES,
            &send->length,
            DEVICE_EXEC_REPORT_V1_TYPE_ID,
            1,
            request_id,
            diagnostic,
            diagnostic_length
        )) {
        send->consumed_callback(send, false);
        free(send);
        return false;
    }

    if (!websocket_send_put(event_loop, send)) {
        send->consumed_callback(send, false);
        free(send);
        return false;
    }

    return true;
}

bool websocket_send_patch_response(
    EventLoop *event_loop,
    uint32_t request_id,
    IpcStatus status
) {
    static const uint8_t failed[] = "patch could not be applied exactly";
    static const uint8_t uncertain[] = "patch result is uncertain";
    DeviceApplyPatchResponseV1 message = {0};
    WebSocketSend *send = malloc(sizeof(*send));

    if (send == NULL) return false;
    send->bytes = malloc(DEVICE_MAXIMUM_FRAME_BYTES);
    if (send->bytes == NULL) {
        free(send);
        return false;
    }

    send->sent = 0;
    send->consumed_callback = websocket_bytes_consumed;
    if (status == IPC_STATUS_OK) {
        if (!device_encode_apply_patch_response_v1(
                send->bytes,
                DEVICE_MAXIMUM_FRAME_BYTES,
                &send->length,
                request_id,
                &message
            )) goto failed;
    } else {
        const uint8_t *diagnostic = status == IPC_STATUS_UNCERTAIN ? uncertain : failed;
        size_t diagnostic_length = status == IPC_STATUS_UNCERTAIN
            ? sizeof(uncertain) - 1
            : sizeof(failed) - 1;

        if (!device_encode_failure(
                send->bytes,
                DEVICE_MAXIMUM_FRAME_BYTES,
                &send->length,
                DEVICE_APPLY_PATCH_RESPONSE_V1_TYPE_ID,
                1,
                request_id,
                diagnostic,
                diagnostic_length
            )) goto failed;
    }

    if (!websocket_send_put(event_loop, send)) goto failed;
    return true;

failed:
    send->consumed_callback(send, false);
    free(send);
    return false;
}

bool websocket_send_file_stat_response(
    EventLoop *event_loop,
    uint32_t request_id,
    IpcStatus status,
    const IpcFileStatResponse *stat
) {
    static const uint8_t failed[] = "file stat failed";
    static const uint8_t not_found[] = "file not found";
    DeviceFileStatResponseV1 message = {0};
    size_t capacity = DEVICE_FRAME_HEADER_BYTES + 10 + sizeof(message.sha256);
    WebSocketSend *send = malloc(sizeof(*send));
    bool encoded;

    if (send == NULL) return false;
    send->bytes = malloc(capacity);
    if (send->bytes == NULL) {
        free(send);
        return false;
    }
    send->sent = 0;
    send->consumed_callback = websocket_bytes_consumed;
    if (status == IPC_STATUS_OK) {
        message.size_bytes = stat->size_bytes;
        memcpy(message.sha256, stat->sha256, sizeof(message.sha256));
        encoded = device_encode_file_stat_response_v1(
            send->bytes, capacity, &send->length, request_id, &message
        );
    } else {
        const uint8_t *diagnostic = status == IPC_STATUS_NOT_FOUND ? not_found : failed;
        size_t diagnostic_length = status == IPC_STATUS_NOT_FOUND
            ? sizeof(not_found) - 1 : sizeof(failed) - 1;

        encoded = device_encode_failure(
            send->bytes, capacity, &send->length,
            DEVICE_FILE_STAT_RESPONSE_V1_TYPE_ID, 1, request_id,
            diagnostic, diagnostic_length
        );
    }
    if (!encoded || !websocket_send_put(event_loop, send)) {
        send->consumed_callback(send, false);
        free(send);
        return false;
    }
    return true;
}

bool websocket_send_file_transfer_response(
    EventLoop *event_loop,
    uint32_t request_id,
    IpcRequestType type,
    IpcStatus status
) {
    static const uint8_t upload_failed[] = "file upload failed";
    static const uint8_t download_failed[] = "file download failed";
    DeviceFileUploadResponseV1 upload = {0};
    DeviceFileDownloadResponseV1 download = {0};
    WebSocketSend *send = malloc(sizeof(*send));
    size_t capacity = DEVICE_FRAME_HEADER_BYTES + sizeof(download_failed);
    bool encoded;

    if (send == NULL) return false;
    send->bytes = malloc(capacity);
    if (send->bytes == NULL) {
        free(send);
        return false;
    }
    send->sent = 0;
    send->consumed_callback = websocket_bytes_consumed;
    if (status == IPC_STATUS_OK && type == IPC_REQUEST_FILE_UPLOAD) {
        encoded = device_encode_file_upload_response_v1(
            send->bytes, capacity, &send->length, request_id, &upload
        );
    } else if (status == IPC_STATUS_OK && type == IPC_REQUEST_FILE_DOWNLOAD) {
        encoded = device_encode_file_download_response_v1(
            send->bytes, capacity, &send->length, request_id, &download
        );
    } else {
        const uint8_t *diagnostic = type == IPC_REQUEST_FILE_UPLOAD
            ? upload_failed : download_failed;
        size_t diagnostic_length = type == IPC_REQUEST_FILE_UPLOAD
            ? sizeof(upload_failed) - 1 : sizeof(download_failed) - 1;
        uint16_t response_type = type == IPC_REQUEST_FILE_UPLOAD
            ? DEVICE_FILE_UPLOAD_RESPONSE_V1_TYPE_ID
            : DEVICE_FILE_DOWNLOAD_RESPONSE_V1_TYPE_ID;

        encoded = device_encode_failure(
            send->bytes, capacity, &send->length,
            response_type, 1, request_id, diagnostic, diagnostic_length
        );
    }
    if (!encoded || !websocket_send_put(event_loop, send)) {
        send->consumed_callback(send, false);
        free(send);
        return false;
    }
    return true;
}

void websocket_send_heartbeat(uv_timer_t *timer) {
    EventLoop *event_loop = timer->data;
    DeviceHeartbeatRequestV1 message = {0};
    WebSocketSend *send = malloc(sizeof(*send));
    uint32_t request_id;

    if (send == NULL) {
        logf_error("failed to allocate Heartbeat request");
        return;
    }

    send->bytes = malloc(DEVICE_MAXIMUM_FRAME_BYTES);

    if (send->bytes == NULL) {
        free(send);
        logf_error("failed to allocate Heartbeat request bytes");
        return;
    }

    request_id = (uint32_t)++event_loop->next_request_id;
    if (request_id == 0) request_id = (uint32_t)++event_loop->next_request_id;

    send->sent = 0;
    send->consumed_callback = websocket_bytes_consumed;

    if (!device_encode_heartbeat_request_v1(
            send->bytes,
            DEVICE_MAXIMUM_FRAME_BYTES,
            &send->length,
            request_id,
            &message
        ) ||
        !websocket_send_put(event_loop, send)) {
        send->consumed_callback(send, false);
        free(send);
        logf_error("failed to queue Heartbeat request");
    }
}

void websocket_send_clear(EventLoop *event_loop) {
    WebSocketSend *send;

    uv_timer_stop(&event_loop->websocket_send_timer);

    /* Unsent frames belong to the failed connection and are never retried. */
    while ((send = circular_queue_consume(
                &event_loop->websocket_send_queue
            )) != NULL) {
        send->consumed_callback(send, false);
        free(send);
    }
}
