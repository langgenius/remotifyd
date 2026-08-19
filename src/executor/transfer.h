#ifndef REMOTIFY_EXECUTOR_TRANSFER_H
#define REMOTIFY_EXECUTOR_TRANSFER_H

#include "executor/device_protocol_gen.h"

#include <stdbool.h>

typedef struct TransferClient {
    const char *api_url;
    const char *credential;
} TransferClient;

bool transfer_file_stat(
    const DeviceFileStatRequestV1 *request,
    DeviceFileStatResponseV1 *response
);
bool transfer_file_upload(
    const TransferClient *client,
    const DeviceFileUploadRequestV1 *request
);
bool transfer_file_download(
    const TransferClient *client,
    const DeviceFileDownloadRequestV1 *request
);

#endif
