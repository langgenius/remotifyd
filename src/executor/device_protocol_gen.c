// Code generated from api/protocol/device/schema/schema.go; DO NOT EDIT.
#include "executor/device_protocol_gen.h"

#include <stdlib.h>
#include <string.h>

typedef struct Reader { const uint8_t *data; size_t length; size_t offset; } Reader;
typedef struct Writer { uint8_t *data; size_t length; size_t capacity; } Writer;

static uint16_t read_be16(const uint8_t *value) { return (uint16_t)((uint16_t)value[0] << 8 | value[1]); }
static uint32_t read_be32(const uint8_t *value) { return (uint32_t)value[0] << 24 | (uint32_t)value[1] << 16 | (uint32_t)value[2] << 8 | value[3]; }
static void write_be16(uint8_t *target, uint16_t value) { target[0] = (uint8_t)(value >> 8); target[1] = (uint8_t)value; }
static void write_be32(uint8_t *target, uint32_t value) { target[0] = (uint8_t)(value >> 24); target[1] = (uint8_t)(value >> 16); target[2] = (uint8_t)(value >> 8); target[3] = (uint8_t)value; }

static bool reader_varuint(Reader *reader, uint64_t *value) { uint64_t result = 0; unsigned shift = 0; while (reader->offset < reader->length && shift < 64) { uint8_t byte = reader->data[reader->offset++]; if (shift == 63 && byte > 1) return false; result |= (uint64_t)(byte & 0x7f) << shift; if ((byte & 0x80) == 0) { *value = result; return true; } shift += 7; } return false; }
static bool reader_u8(Reader *reader, uint8_t *value) { if (reader->offset == reader->length) return false; *value = reader->data[reader->offset++]; return true; }
static bool reader_u64(Reader *reader, uint64_t *value) { return reader_varuint(reader, value); }
static bool reader_i32(Reader *reader, int32_t *value) { uint64_t raw; if (!reader_varuint(reader, &raw) || raw > UINT32_MAX) return false; *value = (int32_t)((uint32_t)(raw >> 1) ^ (uint32_t)-(int32_t)(raw & 1)); return true; }
static bool reader_blob(Reader *reader, uint8_t **data, size_t *size, bool terminate) { uint64_t length; size_t allocated; if (!reader_varuint(reader, &length) || length > SIZE_MAX - (terminate ? 1 : 0) || (size_t)length > reader->length - reader->offset) return false; allocated = (size_t)length + (terminate ? 1 : 0); *data = allocated == 0 ? NULL : malloc(allocated); if (allocated != 0 && *data == NULL) return false; if (length != 0) memcpy(*data, reader->data + reader->offset, (size_t)length); if (terminate) (*data)[length] = '\0'; *size = (size_t)length; reader->offset += (size_t)length; return true; }
static bool reader_bytes(Reader *reader, DeviceBytes *value) { return reader_blob(reader, &value->data, &value->length, false); }
static bool reader_string(Reader *reader, DeviceString *value) { return reader_blob(reader, (uint8_t **)&value->data, &value->length, true); }
static bool reader_fixed(Reader *reader, uint8_t *value, size_t length) { uint64_t encoded_length; if (!reader_varuint(reader, &encoded_length) || encoded_length != length || length > reader->length - reader->offset) return false; memcpy(value, reader->data + reader->offset, length); reader->offset += length; return true; }
static bool reader_strings(Reader *reader, DeviceStringSlice *value) { uint64_t length; if (!reader_varuint(reader, &length) || length > SIZE_MAX / sizeof(*value->items)) return false; value->length = (size_t)length; value->items = value->length == 0 ? NULL : calloc(value->length, sizeof(*value->items)); if (value->length != 0 && value->items == NULL) return false; for (size_t i = 0; i < value->length; i++) if (!reader_string(reader, &value->items[i])) return false; return true; }
static bool reader_optional_i32(Reader *reader, DeviceOptionalInt32 *value) { uint8_t flag; if (!reader_u8(reader, &flag) || flag > 1) return false; value->present = flag == 0; return !value->present || reader_i32(reader, &value->value); }

static bool writer_reserve(Writer *writer, size_t added) { if (added > DEVICE_PROTOCOL_MAX_FRAME_BYTES - writer->length) return false; size_t required = writer->length + added; if (required <= writer->capacity) return true; size_t capacity = writer->capacity == 0 ? 256 : writer->capacity; while (capacity < required) capacity *= 2; uint8_t *data = realloc(writer->data, capacity); if (data == NULL) return false; writer->data = data; writer->capacity = capacity; return true; }
static bool writer_varuint(Writer *writer, uint64_t value) { uint8_t encoded[10]; size_t length = 0; do { encoded[length] = (uint8_t)(value & 0x7f); value >>= 7; if (value != 0) encoded[length] |= 0x80; length++; } while (value != 0); if (!writer_reserve(writer, length)) return false; memcpy(writer->data + writer->length, encoded, length); writer->length += length; return true; }
static bool writer_u8(Writer *writer, uint8_t value) { if (!writer_reserve(writer, 1)) return false; writer->data[writer->length++] = value; return true; }
static bool writer_u64(Writer *writer, uint64_t value) { return writer_varuint(writer, value); }
static bool writer_i32(Writer *writer, int32_t value) { uint32_t encoded = ((uint32_t)value << 1) ^ (uint32_t)(value >> 31); return writer_varuint(writer, encoded); }
static bool writer_raw(Writer *writer, const uint8_t *value, size_t length) { if (!writer_reserve(writer, length)) return false; if (length != 0) memcpy(writer->data + writer->length, value, length); writer->length += length; return true; }
static bool writer_fixed(Writer *writer, const uint8_t *value, size_t length) { return writer_varuint(writer, length) && writer_raw(writer, value, length); }
static bool writer_bytes(Writer *writer, DeviceBytes value) { return writer_varuint(writer, value.length) && writer_raw(writer, value.data, value.length); }
static bool writer_string(Writer *writer, DeviceString value) { return writer_varuint(writer, value.length) && writer_raw(writer, (const uint8_t *)value.data, value.length); }
static bool writer_strings(Writer *writer, DeviceStringSlice value) { if (!writer_varuint(writer, value.length)) return false; for (size_t i = 0; i < value.length; i++) if (!writer_string(writer, value.items[i])) return false; return true; }
static bool writer_optional_i32(Writer *writer, DeviceOptionalInt32 value) { return writer_u8(writer, value.present ? 0 : 1) && (!value.present || writer_i32(writer, value.value)); }

static bool message_metadata(DeviceMessage *message) {
    switch (message->type_id) {
    case 1: if (message->version != 1) return false; message->role = DEVICE_REQUEST; message->direction = DEVICE_SERVER_TO_EXECUTOR; message->response_type_id = 2; return true;
    case 2: if (message->version != 1) return false; message->role = DEVICE_RESPONSE; message->direction = DEVICE_EXECUTOR_TO_SERVER; return true;
    case 3: if (message->version != 1) return false; message->role = DEVICE_REQUEST; message->direction = DEVICE_EXECUTOR_TO_SERVER; message->response_type_id = 4; return true;
    case 4: if (message->version != 1) return false; message->role = DEVICE_RESPONSE; message->direction = DEVICE_SERVER_TO_EXECUTOR; return true;
    case 5: if (message->version != 1) return false; message->role = DEVICE_REQUEST; message->direction = DEVICE_SERVER_TO_EXECUTOR; message->response_type_id = 6; return true;
    case 6: if (message->version != 1) return false; message->role = DEVICE_RESPONSE; message->direction = DEVICE_EXECUTOR_TO_SERVER; return true;
    case 7: if (message->version != 1) return false; message->role = DEVICE_REQUEST; message->direction = DEVICE_SERVER_TO_EXECUTOR; message->response_type_id = 8; return true;
    case 8: if (message->version != 1) return false; message->role = DEVICE_RESPONSE; message->direction = DEVICE_EXECUTOR_TO_SERVER; return true;
    case 9: if (message->version != 1) return false; message->role = DEVICE_REQUEST; message->direction = DEVICE_SERVER_TO_EXECUTOR; message->response_type_id = 10; return true;
    case 10: if (message->version != 1) return false; message->role = DEVICE_RESPONSE; message->direction = DEVICE_EXECUTOR_TO_SERVER; return true;
    case 11: if (message->version != 1) return false; message->role = DEVICE_REQUEST; message->direction = DEVICE_SERVER_TO_EXECUTOR; message->response_type_id = 12; return true;
    case 12: if (message->version != 1) return false; message->role = DEVICE_RESPONSE; message->direction = DEVICE_EXECUTOR_TO_SERVER; return true;
    default: return false;
    }
}

static bool message_valid(const DeviceMessage *message) {
    switch (message->type_id) {
    case 1: {
        const DeviceExecRunRequestV1 *value = &message->payload.exec_run_request_v1;
        if (value->argv.length == 0) return false;
        for (size_t index = 0; index < value->argv.length; index++) {
            if (value->argv.items[index].length == 0) return false;
        }
        return true;
    }
    case 3: {
        const DeviceExecReportRequestV1 *value = &message->payload.exec_report_request_v1;
        if (value->standard_output.length != 0 && value->standard_error.length != 0) return false;
        if (value->standard_error.length > 64 * 1024 ||
            value->standard_output.length > 64 * 1024 - value->standard_error.length) return false;
        return value->standard_output.length != 0 || value->standard_error.length != 0 || value->exit_code.present;
    }
    case 5:
        return message->payload.file_stat_request_v1.path.length != 0;
    case 7:
        return message->payload.file_upload_request_v1.path.length != 0 &&
            message->payload.file_upload_request_v1.transfer_ticket.length != 0;
    case 9:
        return message->payload.file_download_request_v1.path.length != 0 &&
            message->payload.file_download_request_v1.transfer_ticket.length != 0;
    case 11:
        return message->payload.apply_patch_request_v1.patch.length != 0 &&
            message->payload.apply_patch_request_v1.patch.length <= 8 * 1024 * 1024;
    default:
        return true;
    }
}

bool device_protocol_decode(const uint8_t *frame, size_t length, DeviceMessage *message) {
    Reader reader = {0};
    if (frame == NULL || message == NULL || length < 10 || length > DEVICE_PROTOCOL_MAX_FRAME_BYTES) return false;
    memset(message, 0, sizeof(*message));
    message->type_id = read_be16(frame); message->version = read_be16(frame + 2); message->request_id = read_be32(frame + 4); message->status = (DeviceStatus)read_be16(frame + 8);
    if (!message_metadata(message) || message->status > DEVICE_STATUS_FAILED || (message->role == DEVICE_REQUEST && message->status != DEVICE_STATUS_OK)) return false;
    reader.data = frame + 10; reader.length = length - 10;
    if (message->role == DEVICE_RESPONSE && message->status != DEVICE_STATUS_OK) return reader_string(&reader, &message->diagnostic) && reader.offset == reader.length && message->diagnostic.length > 0 && message->diagnostic.length <= 1024;
    switch (message->type_id) {
    case 1: { DeviceExecRunRequestV1 *value = &message->payload.exec_run_request_v1; return reader_string(&reader, &value->operation_id) && reader_strings(&reader, &value->argv) && reader_string(&reader, &value->cwd) && reader_bytes(&reader, &value->standard_input) && reader.offset == reader.length && message_valid(message); }
    case 2: return reader.offset == reader.length && message_valid(message);
    case 3: { DeviceExecReportRequestV1 *value = &message->payload.exec_report_request_v1; return reader_string(&reader, &value->operation_id) && reader_bytes(&reader, &value->standard_output) && reader_bytes(&reader, &value->standard_error) && reader_optional_i32(&reader, &value->exit_code) && reader.offset == reader.length && message_valid(message); }
    case 4: return reader.offset == reader.length && message_valid(message);
    case 5: { DeviceFileStatRequestV1 *value = &message->payload.file_stat_request_v1; return reader_string(&reader, &value->path) && reader.offset == reader.length && message_valid(message); }
    case 6: { DeviceFileStatResponseV1 *value = &message->payload.file_stat_response_v1; return reader_u64(&reader, &value->size_bytes) && reader_fixed(&reader, value->sha256.bytes, sizeof(value->sha256.bytes)) && reader.offset == reader.length && message_valid(message); }
    case 7: { DeviceFileUploadRequestV1 *value = &message->payload.file_upload_request_v1; return reader_string(&reader, &value->path) && reader_string(&reader, &value->transfer_ticket) && reader.offset == reader.length && message_valid(message); }
    case 8: return reader.offset == reader.length && message_valid(message);
    case 9: { DeviceFileDownloadRequestV1 *value = &message->payload.file_download_request_v1; return reader_string(&reader, &value->path) && reader_string(&reader, &value->transfer_ticket) && reader.offset == reader.length && message_valid(message); }
    case 10: return reader.offset == reader.length && message_valid(message);
    case 11: { DeviceApplyPatchRequestV1 *value = &message->payload.apply_patch_request_v1; return reader_string(&reader, &value->patch) && reader_string(&reader, &value->cwd) && reader.offset == reader.length && message_valid(message); }
    case 12: return reader.offset == reader.length && message_valid(message);
    default: return false;
    }
}

bool device_protocol_encode(const DeviceMessage *message, uint8_t **frame, size_t *length) {
    Writer writer = {0}; DeviceMessage metadata; bool ok = false; if (message == NULL || frame == NULL || length == NULL) return false; metadata = *message; if (!message_metadata(&metadata) || metadata.role != message->role || metadata.direction != message->direction || (metadata.role == DEVICE_REQUEST && metadata.response_type_id != message->response_type_id) || message->status > DEVICE_STATUS_FAILED || (message->role == DEVICE_REQUEST && message->status != DEVICE_STATUS_OK) || !message_valid(message) || !writer_reserve(&writer, 10)) return false; writer.length = 10;
    if (message->role == DEVICE_RESPONSE && message->status != DEVICE_STATUS_OK) ok = message->diagnostic.length > 0 && message->diagnostic.length <= 1024 && writer_string(&writer, message->diagnostic); else switch (message->type_id) {
    case 1: { const DeviceExecRunRequestV1 *value = &message->payload.exec_run_request_v1; ok = writer_string(&writer, value->operation_id) && writer_strings(&writer, value->argv) && writer_string(&writer, value->cwd) && writer_bytes(&writer, value->standard_input); break; }
    case 2: ok = true; break;
    case 3: { const DeviceExecReportRequestV1 *value = &message->payload.exec_report_request_v1; ok = writer_string(&writer, value->operation_id) && writer_bytes(&writer, value->standard_output) && writer_bytes(&writer, value->standard_error) && writer_optional_i32(&writer, value->exit_code); break; }
    case 4: ok = true; break;
    case 5: { const DeviceFileStatRequestV1 *value = &message->payload.file_stat_request_v1; ok = writer_string(&writer, value->path); break; }
    case 6: { const DeviceFileStatResponseV1 *value = &message->payload.file_stat_response_v1; ok = writer_u64(&writer, value->size_bytes) && writer_fixed(&writer, value->sha256.bytes, sizeof(value->sha256.bytes)); break; }
    case 7: { const DeviceFileUploadRequestV1 *value = &message->payload.file_upload_request_v1; ok = writer_string(&writer, value->path) && writer_string(&writer, value->transfer_ticket); break; }
    case 8: ok = true; break;
    case 9: { const DeviceFileDownloadRequestV1 *value = &message->payload.file_download_request_v1; ok = writer_string(&writer, value->path) && writer_string(&writer, value->transfer_ticket); break; }
    case 10: ok = true; break;
    case 11: { const DeviceApplyPatchRequestV1 *value = &message->payload.apply_patch_request_v1; ok = writer_string(&writer, value->patch) && writer_string(&writer, value->cwd); break; }
    case 12: ok = true; break;
    default: break; }
    if (!ok || writer.length > DEVICE_PROTOCOL_MAX_FRAME_BYTES) { free(writer.data); return false; } write_be16(writer.data, message->type_id); write_be16(writer.data + 2, message->version); write_be32(writer.data + 4, message->request_id); write_be16(writer.data + 8, (uint16_t)message->status); *frame = writer.data; *length = writer.length; return true;
}

void device_message_close(DeviceMessage *message) { if (message == NULL) return; free(message->diagnostic.data); switch (message->type_id) {
    case 1: free(message->payload.exec_run_request_v1.operation_id.data); for (size_t i = 0; i < message->payload.exec_run_request_v1.argv.length; i++) free(message->payload.exec_run_request_v1.argv.items[i].data); free(message->payload.exec_run_request_v1.argv.items); free(message->payload.exec_run_request_v1.cwd.data); free(message->payload.exec_run_request_v1.standard_input.data); break;
    case 3: free(message->payload.exec_report_request_v1.operation_id.data); free(message->payload.exec_report_request_v1.standard_output.data); free(message->payload.exec_report_request_v1.standard_error.data); break;
    case 5: free(message->payload.file_stat_request_v1.path.data); break;
    case 7: free(message->payload.file_upload_request_v1.path.data); free(message->payload.file_upload_request_v1.transfer_ticket.data); break;
    case 9: free(message->payload.file_download_request_v1.path.data); free(message->payload.file_download_request_v1.transfer_ticket.data); break;
    case 11: free(message->payload.apply_patch_request_v1.patch.data); free(message->payload.apply_patch_request_v1.cwd.data); break;
    default: break; } memset(message, 0, sizeof(*message)); }
