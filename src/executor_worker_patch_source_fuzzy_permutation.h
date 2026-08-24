#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_PERMUTATION_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_PERMUTATION_H

#include "executor_worker_patch_source_fuzzy_document.h"

typedef struct PatchSourceFuzzyPermutationScratch { uint8_t *backing; }
    PatchSourceFuzzyPermutationScratch;
typedef enum PatchSourceFuzzyPermutationResult {
    PATCH_PERMUTATION_FAILED,
    PATCH_PERMUTATION_CLEAR,
    PATCH_PERMUTATION_FOUND
} PatchSourceFuzzyPermutationResult;

/* Scratch is zero-in/failure-zero and owns one allocation. Provenance must
 * first verify that original and corrected are lockstep aligned. Inputs are
 * used only to measure a request-wide upper bound and are not retained. */
bool patch_source_fuzzy_permutation_scratch_init(
    PatchSourceFuzzyPermutationScratch *result,
    const PatchDocument *original, const PatchSourceFuzzyDocument *corrected
);
/* Scratch must be initialized, and both hunk ranges must name the same aligned
 * block with start <= end <= line_count. FOUND means the checked additions
 * contain a sensitive permutation; CLEAR means none do; FAILED is internal. */
PatchSourceFuzzyPermutationResult patch_source_fuzzy_permutation_original(
    PatchSourceFuzzyPermutationScratch *scratch, const PatchHunk *original,
    const PatchHunk *corrected, size_t start, size_t end
);
/* Candidate is one complete LF-joined addition text borrowed during the call. */
PatchSourceFuzzyPermutationResult patch_source_fuzzy_permutation_text(
    PatchSourceFuzzyPermutationScratch *scratch, const PatchHunk *original,
    const PatchHunk *corrected, size_t start, size_t end, PatchSlice candidate
);
void patch_source_fuzzy_permutation_scratch_release(
    PatchSourceFuzzyPermutationScratch *scratch
);

#endif
