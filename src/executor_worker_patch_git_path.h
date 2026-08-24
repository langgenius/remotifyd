#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_GIT_PATH_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_GIT_PATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct PatchGitPathText {
    const uint8_t *bytes;
    size_t length;
    uint8_t *owned;
} PatchGitPathText;

/* Result must be zero on entry. Unchanged text borrows input; changed text is
 * owned by result. Failure leaves result zero. */
bool patch_git_paths_prepare(
    PatchGitPathText *result,
    const uint8_t *bytes,
    size_t length
);
void patch_git_path_text_release(PatchGitPathText *result);

#endif
