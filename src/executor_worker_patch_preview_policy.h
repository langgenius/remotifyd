#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_PREVIEW_POLICY_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_PREVIEW_POLICY_H

#include "executor_worker_patch_parser.h"

bool patch_preview_policy_allows_file(
    const PatchFile *file,
    size_t source_length
);

#endif
