#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_H

#include "executor_worker_patch_format.h"
#include "executor_worker_patch_source.h"

typedef enum PatchSourceFuzzyResult {
    PATCH_SOURCE_FUZZY_FAILED,
    PATCH_SOURCE_FUZZY_REJECTED,
    PATCH_SOURCE_FUZZY_EXACT,
    PATCH_SOURCE_FUZZY_CANDIDATE
} PatchSourceFuzzyResult;

/* Candidate must be zero on entry. Only CANDIDATE returns owned formatted
 * text; every other result leaves candidate zero. */
PatchSourceFuzzyResult patch_source_fuzzy_build(
    PatchFormattedText *candidate,
    const PatchDocument *original,
    const PatchSourceSet *sources
);

#endif
