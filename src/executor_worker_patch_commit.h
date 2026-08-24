#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_COMMIT_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_COMMIT_H

#include "executor_worker_patch_preview.h"

typedef enum PatchCommitResult {
    PATCH_COMMIT_FAILED,
    PATCH_COMMIT_SUCCEEDED,
    PATCH_COMMIT_UNCERTAIN
} PatchCommitResult;

PatchCommitResult patch_preview_commit(
    uv_loop_t *loop,
    const PatchPreview *preview
);

#endif
