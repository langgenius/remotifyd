package main

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"unicode"

	deviceschema "remotify.dev/remotify/protocol/device/schema"
	"remotify.dev/remotify/protocol/rpcschema"
)

func main() {
	definition := deviceschema.Definition
	if err := definition.Validate(); err != nil {
		panic(err)
	}
	if err := supported(definition); err != nil {
		panic(err)
	}
	directory, err := filepath.Abs(filepath.Join("..", "..", "src", "executor"))
	if err != nil {
		panic(err)
	}
	write(filepath.Join(directory, "device_protocol_gen.h"), header(definition))
	write(filepath.Join(directory, "device_protocol_gen.c"), source(definition))
}

func supported(definition rpcschema.Schema) error {
	for _, method := range definition.Methods {
		for _, message := range []rpcschema.Message{method.Request, method.Response} {
			for _, field := range message.Fields {
				if !supportedType(field.Type) {
					return fmt.Errorf("C generator does not support %s.%s", message.Name, field.Name)
				}
			}
		}
	}
	return nil
}

func supportedType(value rpcschema.Type) bool {
	switch value.Kind {
	case rpcschema.KindByte, rpcschema.KindInt32, rpcschema.KindUint64,
		rpcschema.KindString, rpcschema.KindBytes, rpcschema.KindNamed:
		return true
	case rpcschema.KindSlice:
		return value.Elem != nil && value.Elem.Kind == rpcschema.KindString
	case rpcschema.KindPointer:
		return value.Elem != nil && value.Elem.Kind == rpcschema.KindInt32
	default:
		return false
	}
}

func messages(definition rpcschema.Schema) []rpcschema.Message {
	var result []rpcschema.Message
	for _, method := range definition.Methods {
		result = append(result, method.Request, method.Response)
	}
	return result
}

func cName(value string) string {
	letters := []rune(value)
	var result strings.Builder
	for index, letter := range letters {
		if unicode.IsUpper(letter) && index != 0 &&
			(unicode.IsLower(letters[index-1]) ||
				(index+1 < len(letters) && unicode.IsLower(letters[index+1]))) {
			result.WriteByte('_')
		}
		result.WriteRune(unicode.ToLower(letter))
	}
	name := result.String()
	// C standard-library stream names are macros on several platforms and
	// cannot safely be emitted as struct member tokens.
	switch name {
	case "stdin":
		return "standard_input"
	case "stdout":
		return "standard_output"
	case "stderr":
		return "standard_error"
	default:
		return name
	}
}

func cType(value rpcschema.Type) string {
	switch value.Kind {
	case rpcschema.KindByte:
		return "uint8_t"
	case rpcschema.KindInt32:
		return "int32_t"
	case rpcschema.KindUint64:
		return "uint64_t"
	case rpcschema.KindString:
		return "DeviceString"
	case rpcschema.KindBytes:
		return "DeviceBytes"
	case rpcschema.KindSlice:
		return "DeviceStringSlice"
	case rpcschema.KindPointer:
		return "DeviceOptionalInt32"
	case rpcschema.KindNamed:
		return "Device" + value.Name
	default:
		panic("unsupported C field type")
	}
}

func header(definition rpcschema.Schema) []byte {
	var out bytes.Buffer
	out.WriteString("// Code generated from api/protocol/device/schema/schema.go; DO NOT EDIT.\n")
	out.WriteString("#ifndef REMOTIFY_DEVICE_PROTOCOL_GEN_H\n#define REMOTIFY_DEVICE_PROTOCOL_GEN_H\n\n")
	out.WriteString("#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n\n")
	out.WriteString("enum { DEVICE_PROTOCOL_MAX_FRAME_BYTES = 8519680 };\n\n")
	out.WriteString("typedef struct DeviceBytes { uint8_t *data; size_t length; } DeviceBytes;\n")
	out.WriteString("typedef struct DeviceString { char *data; size_t length; } DeviceString;\n")
	out.WriteString("typedef struct DeviceStringSlice { DeviceString *items; size_t length; } DeviceStringSlice;\n")
	out.WriteString("typedef struct DeviceOptionalInt32 { bool present; int32_t value; } DeviceOptionalInt32;\n")
	for _, fixed := range definition.FixedBytes {
		fmt.Fprintf(&out, "typedef struct Device%s { uint8_t bytes[%d]; } Device%s;\n", fixed.Name, fixed.Length, fixed.Name)
	}
	out.WriteByte('\n')
	for _, message := range messages(definition) {
		fmt.Fprintf(&out, "typedef struct Device%s {\n", message.Name)
		if len(message.Fields) == 0 {
			out.WriteString("    uint8_t unused;\n")
		}
		for _, field := range message.Fields {
			fmt.Fprintf(&out, "    %s %s;\n", cType(field.Type), cName(field.Name))
		}
		fmt.Fprintf(&out, "} Device%s;\n\n", message.Name)
	}
	out.WriteString("typedef enum DeviceRole { DEVICE_REQUEST = 1, DEVICE_RESPONSE = 2 } DeviceRole;\n")
	out.WriteString("typedef enum DeviceDirection { DEVICE_SERVER_TO_EXECUTOR = 1, DEVICE_EXECUTOR_TO_SERVER = 2 } DeviceDirection;\n")
	out.WriteString("typedef enum DeviceStatus {\n")
	out.WriteString("    DEVICE_STATUS_OK = 0,\n")
	out.WriteString("    DEVICE_STATUS_INVALID_REQUEST = 1,\n")
	out.WriteString("    DEVICE_STATUS_UNSUPPORTED_MESSAGE = 2,\n")
	out.WriteString("    DEVICE_STATUS_FAILED = 3\n")
	out.WriteString("} DeviceStatus;\n\n")
	out.WriteString("typedef union DevicePayload {\n")
	for _, message := range messages(definition) {
		fmt.Fprintf(&out, "    Device%s %s;\n", message.Name, cName(message.Name))
	}
	out.WriteString("} DevicePayload;\n\n")
	out.WriteString("typedef struct DeviceMessage {\n")
	out.WriteString("    uint16_t type_id;\n    uint16_t version;\n    uint32_t request_id;\n")
	out.WriteString("    DeviceStatus status;\n    DeviceRole role;\n    DeviceDirection direction;\n")
	out.WriteString("    uint16_t response_type_id;\n    DeviceString diagnostic;\n    DevicePayload payload;\n")
	out.WriteString("} DeviceMessage;\n\n")
	out.WriteString("bool device_protocol_decode(const uint8_t *frame, size_t length, DeviceMessage *message);\n")
	out.WriteString("bool device_protocol_encode(const DeviceMessage *message, uint8_t **frame, size_t *length);\n")
	out.WriteString("void device_message_close(DeviceMessage *message);\n\n#endif\n")
	return out.Bytes()
}

func direction(value rpcschema.Direction) string {
	if value == rpcschema.ServerToExecutor {
		return "DEVICE_SERVER_TO_EXECUTOR"
	}
	return "DEVICE_EXECUTOR_TO_SERVER"
}

func reverse(value rpcschema.Direction) rpcschema.Direction {
	if value == rpcschema.ServerToExecutor {
		return rpcschema.ExecutorToServer
	}
	return rpcschema.ServerToExecutor
}

func decodeField(out *bytes.Buffer, target string, value rpcschema.Type) {
	switch value.Kind {
	case rpcschema.KindByte:
		fmt.Fprintf(out, "reader_u8(&reader, &%s)", target)
	case rpcschema.KindInt32:
		fmt.Fprintf(out, "reader_i32(&reader, &%s)", target)
	case rpcschema.KindUint64:
		fmt.Fprintf(out, "reader_u64(&reader, &%s)", target)
	case rpcschema.KindString:
		fmt.Fprintf(out, "reader_string(&reader, &%s)", target)
	case rpcschema.KindBytes:
		fmt.Fprintf(out, "reader_bytes(&reader, &%s)", target)
	case rpcschema.KindSlice:
		fmt.Fprintf(out, "reader_strings(&reader, &%s)", target)
	case rpcschema.KindPointer:
		fmt.Fprintf(out, "reader_optional_i32(&reader, &%s)", target)
	case rpcschema.KindNamed:
		fmt.Fprintf(out, "reader_fixed(&reader, %s.bytes, sizeof(%s.bytes))", target, target)
	default:
		panic("unsupported decode type")
	}
}

func encodeField(out *bytes.Buffer, target string, value rpcschema.Type) {
	switch value.Kind {
	case rpcschema.KindByte:
		fmt.Fprintf(out, "writer_u8(&writer, %s)", target)
	case rpcschema.KindInt32:
		fmt.Fprintf(out, "writer_i32(&writer, %s)", target)
	case rpcschema.KindUint64:
		fmt.Fprintf(out, "writer_u64(&writer, %s)", target)
	case rpcschema.KindString:
		fmt.Fprintf(out, "writer_string(&writer, %s)", target)
	case rpcschema.KindBytes:
		fmt.Fprintf(out, "writer_bytes(&writer, %s)", target)
	case rpcschema.KindSlice:
		fmt.Fprintf(out, "writer_strings(&writer, %s)", target)
	case rpcschema.KindPointer:
		fmt.Fprintf(out, "writer_optional_i32(&writer, %s)", target)
	case rpcschema.KindNamed:
		fmt.Fprintf(out, "writer_fixed(&writer, %s.bytes, sizeof(%s.bytes))", target, target)
	default:
		panic("unsupported encode type")
	}
}

func source(definition rpcschema.Schema) []byte {
	var out bytes.Buffer
	out.WriteString("// Code generated from api/protocol/device/schema/schema.go; DO NOT EDIT.\n")
	out.WriteString("#include \"executor/device_protocol_gen.h\"\n\n")
	out.WriteString("#include <stdlib.h>\n#include <string.h>\n\n")
	out.WriteString(runtimeSource)
	out.WriteString("\nstatic bool message_metadata(DeviceMessage *message) {\n    switch (message->type_id) {\n")
	for _, method := range definition.Methods {
		fmt.Fprintf(&out, "    case %d: if (message->version != %d) return false; message->role = DEVICE_REQUEST; message->direction = %s; message->response_type_id = %d; return true;\n", method.Request.TypeID, method.Request.Version, direction(method.Direction), method.Response.TypeID)
		fmt.Fprintf(&out, "    case %d: if (message->version != %d) return false; message->role = DEVICE_RESPONSE; message->direction = %s; return true;\n", method.Response.TypeID, method.Response.Version, direction(reverse(method.Direction)))
	}
	out.WriteString("    default: return false;\n    }\n}\n\n")
	out.WriteString(validationSource)
	out.WriteString("bool device_protocol_decode(const uint8_t *frame, size_t length, DeviceMessage *message) {\n")
	out.WriteString("    Reader reader = {0};\n    if (frame == NULL || message == NULL || length < 10 || length > DEVICE_PROTOCOL_MAX_FRAME_BYTES) return false;\n")
	out.WriteString("    memset(message, 0, sizeof(*message));\n    message->type_id = read_be16(frame); message->version = read_be16(frame + 2); message->request_id = read_be32(frame + 4); message->status = (DeviceStatus)read_be16(frame + 8);\n")
	out.WriteString("    if (!message_metadata(message) || message->status > DEVICE_STATUS_FAILED || (message->role == DEVICE_REQUEST && message->status != DEVICE_STATUS_OK)) return false;\n")
	out.WriteString("    reader.data = frame + 10; reader.length = length - 10;\n")
	out.WriteString("    if (message->role == DEVICE_RESPONSE && message->status != DEVICE_STATUS_OK) return reader_string(&reader, &message->diagnostic) && reader.offset == reader.length && message->diagnostic.length > 0 && message->diagnostic.length <= 1024;\n")
	out.WriteString("    switch (message->type_id) {\n")
	for _, message := range messages(definition) {
		if len(message.Fields) == 0 {
			fmt.Fprintf(&out, "    case %d: return reader.offset == reader.length && message_valid(message);\n", message.TypeID)
			continue
		}
		fmt.Fprintf(&out, "    case %d: { Device%s *value = &message->payload.%s; return ", message.TypeID, message.Name, cName(message.Name))
		for index, field := range message.Fields {
			if index != 0 {
				out.WriteString(" && ")
			}
			decodeField(&out, "value->"+cName(field.Name), field.Type)
		}
		out.WriteString(" && reader.offset == reader.length && message_valid(message); }\n")
	}
	out.WriteString("    default: return false;\n    }\n}\n\n")
	out.WriteString("bool device_protocol_encode(const DeviceMessage *message, uint8_t **frame, size_t *length) {\n")
	out.WriteString("    Writer writer = {0}; DeviceMessage metadata; bool ok = false; if (message == NULL || frame == NULL || length == NULL) return false; metadata = *message; if (!message_metadata(&metadata) || metadata.role != message->role || metadata.direction != message->direction || (metadata.role == DEVICE_REQUEST && metadata.response_type_id != message->response_type_id) || message->status > DEVICE_STATUS_FAILED || (message->role == DEVICE_REQUEST && message->status != DEVICE_STATUS_OK) || !message_valid(message) || !writer_reserve(&writer, 10)) return false; writer.length = 10;\n")
	out.WriteString("    if (message->role == DEVICE_RESPONSE && message->status != DEVICE_STATUS_OK) ok = message->diagnostic.length > 0 && message->diagnostic.length <= 1024 && writer_string(&writer, message->diagnostic); else switch (message->type_id) {\n")
	for _, message := range messages(definition) {
		if len(message.Fields) == 0 {
			fmt.Fprintf(&out, "    case %d: ok = true; break;\n", message.TypeID)
			continue
		}
		fmt.Fprintf(&out, "    case %d: { const Device%s *value = &message->payload.%s; ok = ", message.TypeID, message.Name, cName(message.Name))
		for index, field := range message.Fields {
			if index != 0 {
				out.WriteString(" && ")
			}
			encodeField(&out, "value->"+cName(field.Name), field.Type)
		}
		out.WriteString("; break; }\n")
	}
	out.WriteString("    default: break; }\n")
	out.WriteString("    if (!ok || writer.length > DEVICE_PROTOCOL_MAX_FRAME_BYTES) { free(writer.data); return false; } write_be16(writer.data, message->type_id); write_be16(writer.data + 2, message->version); write_be32(writer.data + 4, message->request_id); write_be16(writer.data + 8, (uint16_t)message->status); *frame = writer.data; *length = writer.length; return true;\n}\n\n")
	out.WriteString("void device_message_close(DeviceMessage *message) { if (message == NULL) return; free(message->diagnostic.data); switch (message->type_id) {\n")
	for _, message := range messages(definition) {
		var frees []string
		for _, field := range message.Fields {
			target := "message->payload." + cName(message.Name) + "." + cName(field.Name)
			switch field.Type.Kind {
			case rpcschema.KindString:
				frees = append(frees, "free("+target+".data);")
			case rpcschema.KindBytes:
				frees = append(frees, "free("+target+".data);")
			case rpcschema.KindSlice:
				frees = append(frees, "for (size_t i = 0; i < "+target+".length; i++) free("+target+".items[i].data); free("+target+".items);")
			}
		}
		if len(frees) != 0 {
			fmt.Fprintf(&out, "    case %d: %s break;\n", message.TypeID, strings.Join(frees, " "))
		}
	}
	out.WriteString("    default: break; } memset(message, 0, sizeof(*message)); }\n")
	return out.Bytes()
}

func write(path string, contents []byte) {
	if err := os.WriteFile(path, contents, 0o644); err != nil {
		panic(err)
	}
}

const validationSource = `static bool message_valid(const DeviceMessage *message) {
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

`

const runtimeSource = `typedef struct Reader { const uint8_t *data; size_t length; size_t offset; } Reader;
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
`
