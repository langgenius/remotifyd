#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_MODEL_ESCAPE_DOCUMENT_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_MODEL_ESCAPE_DOCUMENT_H

#include "executor_worker_patch_parser.h"

typedef struct PatchModelEscapeDocument {
    PatchDocument document;
    PatchHunk *hunks_end;
    PatchLine *lines_end;
    uint8_t *content_cursor;
    uint8_t *content_end;
} PatchModelEscapeDocument;

/* Result must be zero on entry and stays zero on failure. Success owns one
 * allocation, while paths, headers, and unchanged contents borrow original;
 * keep both original and its patch bytes alive until formatting/release,
 * and release only through this module. */
bool patch_model_escape_document_init(
    PatchModelEscapeDocument *result, const PatchDocument *original
);
bool patch_model_escape_document_append_original(
    PatchModelEscapeDocument *document, PatchHunk *hunk, const PatchLine *line
);
bool patch_model_escape_document_append_decoded(
    PatchModelEscapeDocument *document, PatchHunk *hunk,
    const PatchLine *line, const PatchSlice *segments, size_t segment_count
);
void patch_model_escape_document_release(PatchModelEscapeDocument *document);

#endif
