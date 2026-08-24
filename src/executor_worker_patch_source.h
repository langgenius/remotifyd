#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_H

#include "executor_worker_patch_parser.h"

#include <uv.h>

typedef struct PatchSourceText {
    uint8_t *bytes;
    size_t length;
    int mode;
} PatchSourceText;

typedef struct PatchSourceEntry {
    PatchSlice path;
    PatchSourceText text;
} PatchSourceEntry;

typedef struct PatchSourceSet {
    PatchSourceEntry *entries;
    size_t count;
} PatchSourceSet;

/* Result must be zero on entry; success owns bytes and failure stays zero. */
bool patch_source_read_resolved(
    uv_loop_t *loop,
    const char *path,
    PatchSourceText *result
);
void patch_source_text_release(PatchSourceText *result);

/* Result must be zero on entry; failure leaves it zero. Entry paths borrow
 * document storage, so the owning set must be released before the document. */
bool patch_source_set_build(
    PatchSourceSet *result,
    uv_loop_t *loop,
    const uint8_t *cwd,
    size_t cwd_length,
    const PatchDocument *document
);
const PatchSourceText *patch_source_set_find(
    const PatchSourceSet *set,
    PatchSlice path
);
void patch_source_set_release(PatchSourceSet *set);

#endif
