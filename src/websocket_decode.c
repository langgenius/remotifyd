#include "websocket_decode.h"

#include "device_protocol_gen.h"
#include "executor.h"
#include "ipc_request_types.h"

#include <string.h>

static bool websocket_decode_exec_run(
    EventLoop *event_loop,
    const DeviceFrameHeader *header,
    const uint8_t *payload,
    size_t payload_length
) {
    DeviceExecRunRequestV1 request;
    IpcExecRequestHeader ipc_request;
    uint8_t body[sizeof(IpcExecRequestHeader) + DEVICE_MAXIMUM_FRAME_BYTES];
    uint8_t *ipc_body = body + sizeof(IpcExecRequestHeader);
    IpcHeader ipc_header;
    bool dispatched;

    if (!device_decode_exec_run_request_v1(payload, payload_length, &request)) return false;
    if (request.operation_id_length != IPC_OPERATION_ID_LENGTH || request.cmd_length == 0) return false;

    /*
     * Generated string and byte fields point directly into the WebSocket
     * message. Copy them into one IPC frame before that message is reused by
     * the read loop; no decoded network allocation needs its own lifetime.
     */
    memcpy(ipc_request.operation_id, request.operation_id, IPC_OPERATION_ID_LENGTH);
    ipc_request.cmd_length = (uint32_t)request.cmd_length;
    ipc_request.cwd_length = (uint32_t)request.cwd_length;
    ipc_request.standard_input_length = (uint32_t)request.standard_input_length;
    ipc_request.env_length = (uint32_t)request.env_length;
    memcpy(body, &ipc_request, sizeof(ipc_request));

    memcpy(ipc_body, request.cmd, request.cmd_length);
    ipc_body += request.cmd_length;
    memcpy(ipc_body, request.cwd, request.cwd_length);
    ipc_body += request.cwd_length;
    memcpy(ipc_body, request.standard_input, request.standard_input_length);
    ipc_body += request.standard_input_length;
    memcpy(ipc_body, request.env, request.env_length);

    ipc_header = (IpcHeader){
        .request_id = header->request_id,
        .body_length = (uint32_t)(
            sizeof(ipc_request) +
            request.cmd_length +
            request.cwd_length +
            request.standard_input_length +
            request.env_length
        ),
        .type = IPC_REQUEST_EXEC
    };

    /*
     * Executor responses retain the device request ID, so the parent can
     * recover this operation-specific callback without teaching its pipe
     * readloop about Exec. Each response refreshes the existing callback TTL.
     */
    if (!ipc_process_callbacks_put(
            &event_loop->callbacks,
            uv_now(&event_loop->loop),
            30 * 60 * 1000,
            header->request_id,
            event_loop_executor_handle
        )) {
        return false;
    }

    dispatched = dispatch_request_to_executor(event_loop, &ipc_header, body);

    if (!dispatched) {
        ipc_process_callbacks_remove(&event_loop->callbacks, header->request_id);
    }

    return dispatched;
}

static bool websocket_decode_heartbeat_response(
    const uint8_t *payload,
    size_t payload_length
) {
    DeviceHeartbeatResponseV1 response;

    /* Heartbeat only confirms that the backend refreshed this connection. */
    return device_decode_heartbeat_response_v1(payload, payload_length, &response);
}

bool websocket_decode(
    EventLoop *event_loop,
    const uint8_t *message,
    size_t message_length
) {
    DeviceFrameHeader header;
    const uint8_t *payload;
    size_t payload_length;

    if (!device_decode_frame_header(message, message_length, &header)) return false;
    if (header.status != DEVICE_STATUS_OK) return false;

    payload = message + DEVICE_FRAME_HEADER_BYTES;
    payload_length = message_length - DEVICE_FRAME_HEADER_BYTES;

    if (header.type_id == DEVICE_EXEC_RUN_REQUEST_V1_TYPE_ID && header.version == 1) {
        return websocket_decode_exec_run(event_loop, &header, payload, payload_length);
    }

    if (header.type_id == DEVICE_HEARTBEAT_RESPONSE_V1_TYPE_ID && header.version == 1) {
        return websocket_decode_heartbeat_response(payload, payload_length);
    }

    return false;
}
