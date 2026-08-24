#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_CANDIDATES_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_CANDIDATES_H

#include "executor_worker_patch_source_fuzzy.h"

typedef struct PatchCandidatePlan {
    PatchFormattedText texts[3];
    size_t count;
} PatchCandidatePlan;

/* Result must be zero on entry and stays zero on failure. Success owns up to
 * three candidate texts in Echo's fixed adjustment order. Inputs are borrowed. */
bool patch_candidate_plan_build(
    PatchCandidatePlan *result, PatchSlice original_text,
    const PatchDocument *original, const PatchSourceSet *sources
);
void patch_candidate_plan_release(PatchCandidatePlan *plan);

#endif
