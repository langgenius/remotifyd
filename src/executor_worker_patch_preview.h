#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_PREVIEW_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_PREVIEW_H

#include "executor_worker_patch_apply_exact.h"

#include <uv.h>

typedef enum PatchFileOperation {
    PATCH_FILE_CREATE,
    PATCH_FILE_MODIFY,
    PATCH_FILE_REMOVE,
    PATCH_FILE_RENAME
} PatchFileOperation;

typedef struct PatchPreviewFile {
    PatchFileOperation operation;
    char *source_path;
    char *target_path;
    uint8_t *source;
    size_t source_length;
    PatchAppliedText target;
    int mode;
} PatchPreviewFile;

typedef struct PatchPreview {
    char *root;
    PatchPreviewFile *files;
    size_t file_count;
} PatchPreview;

typedef enum PatchPreviewResult {
    PATCH_PREVIEW_FAILED,
    PATCH_PREVIEW_MISMATCH,
    PATCH_PREVIEW_SUCCEEDED
} PatchPreviewResult;

/* Result must be zero on entry and owns data only when SUCCEEDED. MISMATCH may
 * enter the fixed adjusted candidates; FAILED must not enter adjustment. */
PatchPreviewResult patch_preview_build(
    PatchPreview *result,
    uv_loop_t *loop,
    const uint8_t *cwd,
    size_t cwd_length,
    const PatchDocument *document
);
/* Rechecks paths and snapshots before commit mutates requested files. */
bool patch_preview_validate(uv_loop_t *loop, const PatchPreview *preview);
void patch_preview_release(PatchPreview *preview);

#endif
