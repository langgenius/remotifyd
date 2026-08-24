#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_JSON_UNICODE_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_JSON_UNICODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum PatchJsonUnicodeResult {
    PATCH_JSON_UNICODE_FAILED,
    PATCH_JSON_UNICODE_REJECTED,
    PATCH_JSON_UNICODE_UNCHANGED,
    PATCH_JSON_UNICODE_DECODED
} PatchJsonUnicodeResult;

typedef struct PatchJsonUnicodeText {
    const uint8_t *bytes;
    size_t length;
    uint8_t *owned;
} PatchJsonUnicodeText;

/* UNCHANGED borrows input; DECODED owns one allocation. FAILED and REJECTED
 * leave the zero-in result unchanged. */
PatchJsonUnicodeResult patch_json_unicode_decode_once(
    PatchJsonUnicodeText *result,
    const uint8_t *bytes,
    size_t length
);
/* Input and output must not overlap. On success, output contains either the
 * unchanged input or its decoded form; failure does not set output_length. */
PatchJsonUnicodeResult patch_json_unicode_decode_once_into(
    uint8_t *output,
    size_t capacity,
    size_t *output_length,
    const uint8_t *bytes,
    size_t length
);
void patch_json_unicode_text_release(PatchJsonUnicodeText *result);

#endif
