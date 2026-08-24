#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_BLOCK_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_BLOCK_H

#include "executor_worker_patch_parser.h"

/* Ranges satisfy start <= end <= line_count. Joined helpers LF-join the
 * selected payloads; change-sides compares only deletion/addition payloads. */
size_t patch_source_fuzzy_operation_count(
    const PatchHunk *hunk, size_t start, size_t end, uint8_t operation
);
bool patch_source_fuzzy_joined_change_sides_equal(
    const PatchHunk *hunk, size_t start, size_t end
);
bool patch_source_fuzzy_joined_operation_empty(
    const PatchHunk *hunk, size_t start, size_t end, uint8_t operation
);

#endif
