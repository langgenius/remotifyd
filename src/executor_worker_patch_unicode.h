#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_UNICODE_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_UNICODE_H

#include "executor_worker_patch_parser.h"

#include <stdint.h>

bool patch_unicode_is_letter_number(int32_t codepoint);

bool patch_unicode_strong_escape_proof(
    const PatchSlice *segments,
    size_t segment_count
);

#endif
