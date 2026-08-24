#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_MATCH_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_MATCH_H

#include "executor_worker_patch_parser.h"

typedef struct PatchSourceFuzzyHunkMatch {
    size_t source_start;
    uint8_t character_edits;
} PatchSourceFuzzyHunkMatch;

typedef struct PatchSourceFuzzyMatchPlan {
    PatchSourceFuzzyHunkMatch *hunks;
    const PatchSlice *source_lines;
    size_t source_line_count;
} PatchSourceFuzzyMatchPlan;

typedef enum PatchSourceFuzzyMatchResult {
    PATCH_FUZZY_MATCH_FAILED,
    PATCH_FUZZY_MATCH_REJECTED,
    PATCH_FUZZY_MATCH_SUCCEEDED
} PatchSourceFuzzyMatchResult;

/* This matcher accepts one known non-creation, exact-mismatch file and a
 * character_edit_budget no greater than three. Plan must be zero on entry.
 * FAILED and REJECTED leave plan zero. A successful plan owns one allocation,
 * while its source_lines borrow source bytes. */
PatchSourceFuzzyMatchResult patch_source_fuzzy_match_file(
    PatchSourceFuzzyMatchPlan *plan,
    const PatchFile *file,
    const uint8_t *source,
    size_t source_length,
    uint8_t character_edit_budget
);
void patch_source_fuzzy_match_plan_release(PatchSourceFuzzyMatchPlan *plan);

#endif
