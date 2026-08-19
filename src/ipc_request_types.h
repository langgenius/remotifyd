#ifndef REMOTIFY_IPC_REQUEST_TYPES_H
#define REMOTIFY_IPC_REQUEST_TYPES_H

#include "ipc_types.h"

typedef enum IpcRequestType {
    IPC_REQUEST_EXEC = 1
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

#endif
