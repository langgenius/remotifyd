#include "executor_worker_patch_json_unicode.h"

#include <stdlib.h>
#include <string.h>

typedef struct PatchJsonUnicodePass {
    uint8_t *output;
    size_t length;
    size_t capacity;
    bool changed;
} PatchJsonUnicodePass;

typedef enum PatchJsonWalkResult {
    PATCH_JSON_WALK_FAILED,
    PATCH_JSON_WALK_REJECTED,
    PATCH_JSON_WALK_SUCCEEDED
} PatchJsonWalkResult;

static bool patch_json_append(
    PatchJsonUnicodePass *pass,
    const uint8_t *bytes,
    size_t length
) {
    if (length > SIZE_MAX - pass->length) return false;
    if (pass->output != NULL) {
        if (length > pass->capacity - pass->length) return false;
        if (length != 0) memcpy(pass->output + pass->length, bytes, length);
    }
    pass->length += length;
    return true;
}

static int patch_json_hex(uint8_t byte) {
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

static bool patch_json_code_unit(
    const uint8_t *bytes,
    size_t length,
    size_t start,
    uint16_t *unit,
    size_t *end
) {
    uint16_t value = 0;
    size_t index;

    if (start > length || length - start < 6 ||
        bytes[start] != '\\' || bytes[start + 1] != 'u') return false;
    for (index = start + 2; index < start + 6; index++) {
        int digit = patch_json_hex(bytes[index]);

        if (digit < 0) return false;
        value = (uint16_t)(value * 16 + (uint16_t)digit);
    }
    *unit = value;
    *end = start + 6;
    return true;
}

static bool patch_json_codepoint(PatchJsonUnicodePass *pass, uint32_t value) {
    uint8_t encoded[4];
    size_t length;

    if (value <= 0x7f) {
        encoded[0] = (uint8_t)value;
        length = 1;
    } else if (value <= 0x7ff) {
        encoded[0] = (uint8_t)(0xc0 | value >> 6);
        encoded[1] = (uint8_t)(0x80 | (value & 0x3f));
        length = 2;
    } else if (value <= 0xffff) {
        encoded[0] = (uint8_t)(0xe0 | value >> 12);
        encoded[1] = (uint8_t)(0x80 | ((value >> 6) & 0x3f));
        encoded[2] = (uint8_t)(0x80 | (value & 0x3f));
        length = 3;
    } else {
        encoded[0] = (uint8_t)(0xf0 | value >> 18);
        encoded[1] = (uint8_t)(0x80 | ((value >> 12) & 0x3f));
        encoded[2] = (uint8_t)(0x80 | ((value >> 6) & 0x3f));
        encoded[3] = (uint8_t)(0x80 | (value & 0x3f));
        length = 4;
    }
    return patch_json_append(pass, encoded, length);
}

static PatchJsonWalkResult patch_json_walk(
    PatchJsonUnicodePass *pass,
    const uint8_t *bytes,
    size_t length
) {
    size_t cursor = 0;

    while (cursor < length) {
        size_t run_end;
        size_t escape_start;
        size_t first_end;
        uint16_t first;
        uint32_t codepoint;

        if (bytes[cursor] != '\\') {
            if (!patch_json_append(pass, bytes + cursor, 1)) return PATCH_JSON_WALK_FAILED;
            cursor++;
            continue;
        }
        run_end = cursor + 1;
        while (run_end < length && bytes[run_end] == '\\') run_end++;
        if (run_end == length || bytes[run_end] != 'u' ||
            (run_end - cursor) % 2 == 0) {
            if (!patch_json_append(pass, bytes + cursor, run_end - cursor)) {
                return PATCH_JSON_WALK_FAILED;
            }
            cursor = run_end;
            continue;
        }
        escape_start = run_end - 1;
        if (!patch_json_append(pass, bytes + cursor, escape_start - cursor) ||
            !patch_json_code_unit(bytes, length, escape_start, &first, &first_end) ||
            first == 0x000a || first == 0x000d) return PATCH_JSON_WALK_REJECTED;
        if (first >= 0xd800 && first <= 0xdbff) {
            uint16_t second;
            size_t second_end;

            if (!patch_json_code_unit(
                    bytes, length, first_end, &second, &second_end
                ) || second < 0xdc00 || second > 0xdfff) {
                return PATCH_JSON_WALK_REJECTED;
            }
            codepoint = 0x10000u + (((uint32_t)first - 0xd800u) << 10) +
                ((uint32_t)second - 0xdc00u);
            cursor = second_end;
        } else {
            if (first >= 0xdc00 && first <= 0xdfff) return PATCH_JSON_WALK_REJECTED;
            codepoint = first;
            cursor = first_end;
        }
        if (!patch_json_codepoint(pass, codepoint)) return PATCH_JSON_WALK_FAILED;
        pass->changed = true;
    }
    return PATCH_JSON_WALK_SUCCEEDED;
}

PatchJsonUnicodeResult patch_json_unicode_decode_once(
    PatchJsonUnicodeText *result,
    const uint8_t *bytes,
    size_t length
) {
    PatchJsonUnicodePass measure = {0};
    PatchJsonUnicodePass write = {0};
    PatchJsonUnicodeText decoded = {0};
    PatchJsonWalkResult walk = patch_json_walk(&measure, bytes, length);

    if (walk == PATCH_JSON_WALK_FAILED) return PATCH_JSON_UNICODE_FAILED;
    if (walk == PATCH_JSON_WALK_REJECTED) return PATCH_JSON_UNICODE_REJECTED;
    if (!measure.changed) {
        decoded.bytes = bytes;
        decoded.length = length;
        *result = decoded;
        return PATCH_JSON_UNICODE_UNCHANGED;
    }
    decoded.owned = malloc(measure.length);
    if (decoded.owned == NULL) return PATCH_JSON_UNICODE_FAILED;
    write.output = decoded.owned;
    write.capacity = measure.length;
    walk = patch_json_walk(&write, bytes, length);
    if (walk != PATCH_JSON_WALK_SUCCEEDED || !write.changed ||
        write.length != measure.length) {
        free(decoded.owned);
        return PATCH_JSON_UNICODE_FAILED;
    }
    decoded.bytes = decoded.owned;
    decoded.length = write.length;
    *result = decoded;
    return PATCH_JSON_UNICODE_DECODED;
}

PatchJsonUnicodeResult patch_json_unicode_decode_once_into(
    uint8_t *output,
    size_t capacity,
    size_t *output_length,
    const uint8_t *bytes,
    size_t length
) {
    PatchJsonUnicodePass measure = {0};
    PatchJsonUnicodePass write = {0};
    PatchJsonWalkResult walk = patch_json_walk(&measure, bytes, length);

    if (walk == PATCH_JSON_WALK_FAILED) return PATCH_JSON_UNICODE_FAILED;
    if (walk == PATCH_JSON_WALK_REJECTED) return PATCH_JSON_UNICODE_REJECTED;
    if (measure.length > capacity ||
        (measure.length != 0 && output == NULL)) return PATCH_JSON_UNICODE_FAILED;
    write.output = output;
    write.capacity = capacity;
    walk = patch_json_walk(&write, bytes, length);
    if (walk != PATCH_JSON_WALK_SUCCEEDED ||
        write.changed != measure.changed || write.length != measure.length) {
        return PATCH_JSON_UNICODE_FAILED;
    }
    *output_length = write.length;
    return write.changed ? PATCH_JSON_UNICODE_DECODED
        : PATCH_JSON_UNICODE_UNCHANGED;
}

void patch_json_unicode_text_release(PatchJsonUnicodeText *result) {
    free(result->owned);
    memset(result, 0, sizeof(*result));
}
