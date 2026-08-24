#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_PATH_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_PATH_H

#include "executor_worker_patch_parser.h"

#include <uv.h>

/* Each output must point to NULL on entry; success returns one owned NUL path. */
bool patch_root_resolve(
    uv_loop_t *loop,
    const uint8_t *cwd,
    size_t cwd_length,
    char **root
);
bool patch_path_resolve(
    uv_loop_t *loop,
    const char *root,
    PatchSlice relative,
    char **path
);
/* path must be the mutable owned result of patch_path_resolve for this root. */
bool patch_path_validate(uv_loop_t *loop, const char *root, char *path);
void patch_path_release(char **path);

#endif
