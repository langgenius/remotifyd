#ifndef REMOTIFY_IPC_TYPES_H
#define REMOTIFY_IPC_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { IPC_OPERATION_ID_LENGTH = 36 };

typedef enum IpcResponseType {
    IPC_SINGLE = 0,
    IPC_STREAM = 1,
    IPC_END = 2
} IpcResponseType;

typedef enum IpcStatus {
    IPC_STATUS_OK = 0,
    IPC_STATUS_FAILED = 1,
    IPC_STATUS_UNCERTAIN = 2
} IpcStatus;

typedef struct IpcHeader {
    uint64_t request_id;
    uint32_t body_length;
    uint8_t type;
    uint8_t response_type;
    uint8_t status;
    uint8_t padding;
} IpcHeader;

/*
 * IpcReadBuffer owns only the incomplete frame currently being assembled from
 * one pipe. Pipe handles and request callbacks belong to EventLoop instead.
 */
typedef struct IpcReadBuffer {
    IpcHeader header;
    size_t header_received;

    uint8_t *body;
    size_t body_received;
} IpcReadBuffer;

typedef struct IpcProcessCallbackEntry IpcProcessCallbackEntry;

struct EventLoop;

typedef void (*IpcProcessCallback)(
    struct EventLoop *event_loop,
    uint32_t device_request_id,
    const IpcHeader *header,
    const uint8_t *body
);

/*
 * Pending callbacks form one naturally growing singly linked list. New
 * callbacks are appended at head. tail is the oldest entry and the starting
 * point for ordered TTL expiration scans.
 */
typedef struct IpcProcessCallbacks {
    IpcProcessCallbackEntry *head;
    IpcProcessCallbackEntry *tail;
} IpcProcessCallbacks;

bool ipc_process_callbacks_put(IpcProcessCallbacks *callbacks, uint64_t now, uint64_t ttl, uint32_t device_request_id, IpcProcessCallback callback);
IpcProcessCallback ipc_process_callbacks_get(IpcProcessCallbacks *callbacks, uint64_t now, uint32_t device_request_id);
void ipc_process_callbacks_remove(IpcProcessCallbacks *callbacks, uint32_t device_request_id);
void ipc_process_callbacks_clear(IpcProcessCallbacks *callbacks);

#endif
