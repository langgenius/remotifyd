#ifndef REMOTIFY_IPC_RESPONSE_TYPES_H
#define REMOTIFY_IPC_RESPONSE_TYPES_H

#include "ipc_types.h"

/*
 * stdout, stderr, and error bytes follow this fixed header in that order. One
 * structure represents process start, streaming output, process end, and a
 * concrete spawn failure without adding another IPC response type.
 */
typedef struct IpcExecReportHeader {
    char operation_id[IPC_OPERATION_ID_LENGTH];
    uint64_t local_process_id;
    int32_t exit_code;
    uint32_t stdout_length;
    uint32_t stderr_length;
    uint32_t error_length;
} IpcExecReportHeader;

_Static_assert(sizeof(IpcExecReportHeader) == 64, "unexpected IpcExecReportHeader size");

#endif
