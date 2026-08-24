#include "executor_worker_patch_source_fuzzy_block.h"

#include <string.h>

static bool patch_block_slice_equal(PatchSlice left, PatchSlice right) {
    return left.length == right.length && (left.length == 0 ||
        memcmp(left.bytes, right.bytes, left.length) == 0);
}

size_t patch_source_fuzzy_operation_count(
    const PatchHunk *hunk, size_t start, size_t end, uint8_t operation
) {
    size_t count = 0;
    while (start < end) {
        if (hunk->lines[start].operation == operation) count++;
        start++;
    }
    return count;
}

bool patch_source_fuzzy_joined_change_sides_equal(
    const PatchHunk *hunk, size_t start, size_t end
) {
    size_t deletions = patch_source_fuzzy_operation_count(
        hunk, start, end, '-');
    size_t additions = patch_source_fuzzy_operation_count(
        hunk, start, end, '+');
    size_t deletion_index = start;
    size_t addition_index = start;
    size_t matched = 0;

    if (deletions == 0 || additions == 0) {
        size_t count = deletions == 0 ? additions : deletions;
        uint8_t operation = deletions == 0 ? '+' : '-';
        if (count == 0) return true;
        if (count != 1) return false;
        while (start < end && hunk->lines[start].operation != operation) start++;
        return hunk->lines[start].content.length == 0;
    }
    if (deletions != additions) return false;
    while (matched < deletions) {
        while (deletion_index < end &&
            hunk->lines[deletion_index].operation != '-') deletion_index++;
        while (addition_index < end &&
            hunk->lines[addition_index].operation != '+') addition_index++;
        if (deletion_index == end || addition_index == end) return false;
        if (!patch_block_slice_equal(
            hunk->lines[deletion_index].content,
            hunk->lines[addition_index].content)) return false;
        deletion_index++;
        addition_index++;
        matched++;
    }
    return true;
}

bool patch_source_fuzzy_joined_operation_empty(
    const PatchHunk *hunk, size_t start, size_t end, uint8_t operation
) {
    size_t count = patch_source_fuzzy_operation_count(
        hunk, start, end, operation);
    if (count == 0) return true;
    if (count != 1) return false;
    while (start < end && hunk->lines[start].operation != operation) start++;
    return hunk->lines[start].content.length == 0;
}
