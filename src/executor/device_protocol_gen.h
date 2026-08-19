// Code generated from api/protocol/device/schema/schema.go; DO NOT EDIT.
#ifndef REMOTIFY_DEVICE_PROTOCOL_GEN_H
#define REMOTIFY_DEVICE_PROTOCOL_GEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { DEVICE_PROTOCOL_MAX_FRAME_BYTES = 8519680 };

typedef struct DeviceBytes { uint8_t *data; size_t length; } DeviceBytes;
typedef struct DeviceString { char *data; size_t length; } DeviceString;
typedef struct DeviceStringSlice { DeviceString *items; size_t length; } DeviceStringSlice;
typedef struct DeviceOptionalInt32 { bool present; int32_t value; } DeviceOptionalInt32;
typedef struct DeviceSHA256 { uint8_t bytes[32]; } DeviceSHA256;

typedef struct DeviceExecRunRequestV1 {
    DeviceString operation_id;
    DeviceStringSlice argv;
    DeviceString cwd;
    DeviceBytes standard_input;
} DeviceExecRunRequestV1;

typedef struct DeviceExecRunResponseV1 {
    uint8_t unused;
} DeviceExecRunResponseV1;

typedef struct DeviceExecReportRequestV1 {
    DeviceString operation_id;
    DeviceBytes standard_output;
    DeviceBytes standard_error;
    DeviceOptionalInt32 exit_code;
} DeviceExecReportRequestV1;

typedef struct DeviceExecReportResponseV1 {
    uint8_t unused;
} DeviceExecReportResponseV1;

typedef struct DeviceFileStatRequestV1 {
    DeviceString path;
} DeviceFileStatRequestV1;

typedef struct DeviceFileStatResponseV1 {
    uint64_t size_bytes;
    DeviceSHA256 sha256;
} DeviceFileStatResponseV1;

typedef struct DeviceFileUploadRequestV1 {
    DeviceString path;
    DeviceString transfer_ticket;
} DeviceFileUploadRequestV1;

typedef struct DeviceFileUploadResponseV1 {
    uint8_t unused;
} DeviceFileUploadResponseV1;

typedef struct DeviceFileDownloadRequestV1 {
    DeviceString path;
    DeviceString transfer_ticket;
} DeviceFileDownloadRequestV1;

typedef struct DeviceFileDownloadResponseV1 {
    uint8_t unused;
} DeviceFileDownloadResponseV1;

typedef struct DeviceApplyPatchRequestV1 {
    DeviceString patch;
    DeviceString cwd;
} DeviceApplyPatchRequestV1;

typedef struct DeviceApplyPatchResponseV1 {
    uint8_t unused;
} DeviceApplyPatchResponseV1;

typedef enum DeviceRole { DEVICE_REQUEST = 1, DEVICE_RESPONSE = 2 } DeviceRole;
typedef enum DeviceDirection { DEVICE_SERVER_TO_EXECUTOR = 1, DEVICE_EXECUTOR_TO_SERVER = 2 } DeviceDirection;
typedef enum DeviceStatus {
    DEVICE_STATUS_OK = 0,
    DEVICE_STATUS_INVALID_REQUEST = 1,
    DEVICE_STATUS_UNSUPPORTED_MESSAGE = 2,
    DEVICE_STATUS_FAILED = 3
} DeviceStatus;

typedef union DevicePayload {
    DeviceExecRunRequestV1 exec_run_request_v1;
    DeviceExecRunResponseV1 exec_run_response_v1;
    DeviceExecReportRequestV1 exec_report_request_v1;
    DeviceExecReportResponseV1 exec_report_response_v1;
    DeviceFileStatRequestV1 file_stat_request_v1;
    DeviceFileStatResponseV1 file_stat_response_v1;
    DeviceFileUploadRequestV1 file_upload_request_v1;
    DeviceFileUploadResponseV1 file_upload_response_v1;
    DeviceFileDownloadRequestV1 file_download_request_v1;
    DeviceFileDownloadResponseV1 file_download_response_v1;
    DeviceApplyPatchRequestV1 apply_patch_request_v1;
    DeviceApplyPatchResponseV1 apply_patch_response_v1;
} DevicePayload;

typedef struct DeviceMessage {
    uint16_t type_id;
    uint16_t version;
    uint32_t request_id;
    DeviceStatus status;
    DeviceRole role;
    DeviceDirection direction;
    uint16_t response_type_id;
    DeviceString diagnostic;
    DevicePayload payload;
} DeviceMessage;

bool device_protocol_decode(const uint8_t *frame, size_t length, DeviceMessage *message);
bool device_protocol_encode(const DeviceMessage *message, uint8_t **frame, size_t *length);
void device_message_close(DeviceMessage *message);

#endif
