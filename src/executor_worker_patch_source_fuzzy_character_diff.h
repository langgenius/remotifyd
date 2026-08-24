#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_CHARACTER_DIFF_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_CHARACTER_DIFF_H

#include "executor_worker_patch_parser.h"

#define PATCH_SOURCE_FUZZY_CHARACTER_DIFF_MAX_EDITS 2048u

typedef struct PatchSourceFuzzyCharacterText {
    PatchSlice bytes;
    const size_t *boundaries;
    size_t grapheme_count;
} PatchSourceFuzzyCharacterText;

typedef struct PatchSourceFuzzyCharacterEdit {
    size_t start;
    size_t end;
    size_t replacement_start;
    size_t replacement_count;
} PatchSourceFuzzyCharacterEdit;

typedef struct PatchSourceFuzzyCharacterDiffScratch {
    size_t *trace;
    size_t trace_capacity;
    PatchSourceFuzzyCharacterEdit *forward_edits;
    size_t forward_capacity;
    PatchSourceFuzzyCharacterEdit *reverse_edits;
    size_t reverse_capacity;
} PatchSourceFuzzyCharacterDiffScratch;

typedef struct PatchSourceFuzzyCharacterDiff {
    PatchSourceFuzzyCharacterEdit *variants[2];
    size_t edit_counts[2];
    size_t variant_count;
} PatchSourceFuzzyCharacterDiff;

typedef enum PatchSourceFuzzyCharacterDiffResult {
    PATCH_CHARACTER_DIFF_FAILED,
    PATCH_CHARACTER_DIFF_REJECTED,
    PATCH_CHARACTER_DIFF_SUCCEEDED
} PatchSourceFuzzyCharacterDiffResult;

/* Inputs are valid UTF-8 with count + 1 boundaries. Scratch and result are
 * caller-owned; failure/rejection leave result unspecified. Success returns
 * one or two deduplicated edit lists borrowing scratch until its next use;
 * replacement ranges borrow variant. Rejection means more than 100000
 * graphemes or no path within 2048 edits. */
PatchSourceFuzzyCharacterDiffResult patch_source_fuzzy_character_diff(
    PatchSourceFuzzyCharacterDiff *result,
    PatchSourceFuzzyCharacterDiffScratch *scratch,
    const PatchSourceFuzzyCharacterText *base,
    const PatchSourceFuzzyCharacterText *variant
);

#endif
