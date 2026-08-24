#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_NORMALIZE_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_NORMALIZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum PatchNormalizationChange {
    PATCH_NORMALIZED_EMPTY_CONTEXT = 1u << 0,
    PATCH_NORMALIZED_HUNK_COUNTS = 1u << 1,
    PATCH_NORMALIZED_NULL_START = 1u << 2,
    PATCH_NORMALIZED_TRAILING_LF = 1u << 3
};

typedef struct PatchNormalizedText {
    const uint8_t *bytes;
    size_t length;
    uint8_t changes;
} PatchNormalizedText;

/*
 * changes == 0 borrows input; changes != 0 owns bytes. On failure, result
 * stays zero and the normalizer releases any private temporary allocation.
 */
bool patch_normalize(
    PatchNormalizedText *result,
    const uint8_t *bytes,
    size_t length
);
void patch_normalized_text_release(PatchNormalizedText *result);

#endif
