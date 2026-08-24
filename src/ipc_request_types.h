#ifndef REMOTIFY_IPC_REQUEST_TYPES_H
#define REMOTIFY_IPC_REQUEST_TYPES_H

#include "ipc_types.h"

typedef enum IpcRequestType {
    IPC_REQUEST_EXEC = 1,
    IPC_REQUEST_PATCH = 2,
    IPC_REQUEST_FILE_UPLOAD = 3,
    IPC_REQUEST_FILE_DOWNLOAD = 4,
    IPC_REQUEST_FILE_STAT = 5
} IpcRequestType;

/*
 * The variable data immediately after this header contains cmd, cwd,
 * standard-input, and env bytes in that order. Each length lives in the fixed
 * header because pointers cannot cross the process boundary.
 */
typedef struct IpcExecRequestHeader {
    char operation_id[IPC_OPERATION_ID_LENGTH];
    uint32_t cmd_length;
    uint32_t cwd_length;
    uint32_t standard_input_length;
    uint32_t env_length;
} IpcExecRequestHeader;

_Static_assert(sizeof(IpcExecRequestHeader) == 52, "unexpected IpcExecRequestHeader size");

/* Patch and cwd bytes follow this header in the same order. */
typedef struct IpcPatchRequestHeader {
    uint32_t patch_length;
    uint32_t cwd_length;
} IpcPatchRequestHeader;

_Static_assert(sizeof(IpcPatchRequestHeader) == 8, "unexpected IpcPatchRequestHeader size");

/* Path and opaque signed URL bytes follow this header in that order. */
typedef struct IpcFileTransferRequestHeader {
    uint32_t path_length;
    uint32_t url_length;
} IpcFileTransferRequestHeader;

_Static_assert(sizeof(IpcFileTransferRequestHeader) == 8, "unexpected IpcFileTransferRequestHeader size");

/* Source path bytes follow this header. */
typedef struct IpcFileStatRequestHeader {
    uint32_t path_length;
} IpcFileStatRequestHeader;

_Static_assert(sizeof(IpcFileStatRequestHeader) == 4, "unexpected IpcFileStatRequestHeader size");

#endif
