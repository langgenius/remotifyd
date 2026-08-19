#ifndef REMOTIFY_IPC_RESPONSE_TYPES_H
#define REMOTIFY_IPC_RESPONSE_TYPES_H

#include "ipc_types.h"

/*
 * stdout bytes and stderr bytes follow this fixed header in that order. The
 * same structure is used for IPC_STREAM and IPC_END; only the latter consumes
 * exit_code.
 */
typedef struct IpcExecReportHeader {
    char operation_id[IPC_OPERATION_ID_LENGTH];
    int32_t exit_code;
    uint32_t stdout_length;
    uint32_t stderr_length;
} IpcExecReportHeader;

_Static_assert(sizeof(IpcExecReportHeader) == 48, "unexpected IpcExecReportHeader size");

#endif
