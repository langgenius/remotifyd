#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_APPLY_EXACT_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_APPLY_EXACT_H

#include "executor_worker_patch_parser.h"

typedef struct PatchAppliedText {
    uint8_t *bytes;
    size_t length;
} PatchAppliedText;

typedef enum PatchExactResult {
    PATCH_EXACT_FAILED,
    PATCH_EXACT_MISMATCH,
    PATCH_EXACT_SUCCEEDED
} PatchExactResult;

/*
 * FAILED is a resource, size, or internal failure. MISMATCH means a valid
 * old-side or EOF expectation did not match source. Both leave result zero;
 * success returns one owned allocation.
 */
PatchExactResult patch_file_apply_exact(
    PatchAppliedText *result,
    const uint8_t *source,
    size_t source_length,
    const PatchFile *file
);
void patch_applied_text_release(PatchAppliedText *result);

#endif
