#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_MODEL_ESCAPE_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_MODEL_ESCAPE_H

#include "executor_worker_patch_format.h"
#include "executor_worker_patch_source.h"

typedef enum PatchModelEscapeResult {
    PATCH_MODEL_ESCAPE_FAILED,
    PATCH_MODEL_ESCAPE_REJECTED,
    PATCH_MODEL_ESCAPE_UNCHANGED,
    PATCH_MODEL_ESCAPE_CANDIDATE
} PatchModelEscapeResult;

/* Candidate must be zero on entry. Only CANDIDATE returns owned formatted
 * text; every other result leaves candidate zero. */
PatchModelEscapeResult patch_model_escape_build(
    PatchFormattedText *candidate,
    const PatchDocument *document,
    const PatchSourceSet *sources
);

#endif
