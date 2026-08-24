#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_CHARACTER_MERGE_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_CHARACTER_MERGE_H

#include "executor_worker_patch_source_fuzzy_document.h"

typedef struct PatchSourceFuzzyCharacterMergeScratch { uint8_t *backing; }
    PatchSourceFuzzyCharacterMergeScratch;
typedef enum PatchSourceFuzzyCharacterMergeResult {
    PATCH_CHARACTER_MERGE_FAILED,
    PATCH_CHARACTER_MERGE_REJECTED,
    PATCH_CHARACTER_MERGE_FULLY_REPLACED,
    PATCH_CHARACTER_MERGE_UNCHANGED,
    PATCH_CHARACTER_MERGE_MERGED
} PatchSourceFuzzyCharacterMergeResult;
typedef enum PatchSourceFuzzyCharacterMergeMode {
    PATCH_CHARACTER_MERGE_NORMAL,
    PATCH_CHARACTER_MERGE_ALLOW_FULLY_REPLACED,
    PATCH_CHARACTER_MERGE_FULLY_REPLACED_ONLY
} PatchSourceFuzzyCharacterMergeMode;

/* Scratch must be zero on entry and stays zero on failure. Success owns one
 * backing allocation until this module releases it. Inputs must already be
 * lockstep-aligned by provenance. */
bool patch_source_fuzzy_character_merge_scratch_init(
    PatchSourceFuzzyCharacterMergeScratch *result,
    const PatchDocument *original,
    const PatchSourceFuzzyDocument *corrected
);
/* Start/end must name the same valid change block in both aligned hunks.
 * Only MERGED writes a slice, which borrows scratch until its next use.
 * Mode selects normal merge, rejected-merge fallback, or coverage only. */
PatchSourceFuzzyCharacterMergeResult patch_source_fuzzy_character_merge(
    PatchSlice *merged,
    PatchSourceFuzzyCharacterMergeScratch *scratch,
    const PatchHunk *original,
    const PatchHunk *corrected,
    size_t start,
    size_t end,
    PatchSourceFuzzyCharacterMergeMode mode
);
void patch_source_fuzzy_character_merge_scratch_release(
    PatchSourceFuzzyCharacterMergeScratch *scratch
);

#endif
