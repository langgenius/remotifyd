#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_DOCUMENT_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_DOCUMENT_H

#include "executor_worker_patch_source_fuzzy_match.h"

typedef struct PatchSourceFuzzyDocument {
    PatchDocument document;
} PatchSourceFuzzyDocument;

/* Result must be zero on entry and stays zero on failure. Success owns one
 * files/hunks/lines allocation released only through this module. Paths,
 * headers, and unchanged content borrow the original document and patch bytes;
 * replaced old-side content will borrow source bytes. Keep those owners alive
 * through formatting or release, but a consumed match plan may be freed. */
bool patch_source_fuzzy_document_init(
    PatchSourceFuzzyDocument *result,
    const PatchDocument *original
);
bool patch_source_fuzzy_document_apply_file(
    PatchSourceFuzzyDocument *document,
    size_t file_index,
    const PatchSourceFuzzyMatchPlan *plan
);
void patch_source_fuzzy_document_release(PatchSourceFuzzyDocument *document);

#endif
