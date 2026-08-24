#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_PRESENTATION_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_PRESENTATION_H

#include "executor_worker_patch_source_fuzzy_document.h"
#include "executor_worker_patch_source_fuzzy_unicode.h"

typedef struct PatchSourceFuzzyPresentation {
    uint8_t *backing;
    PatchSourceFuzzyLines comparable;
} PatchSourceFuzzyPresentation;

typedef enum PatchSourceFuzzyPresentationResult {
    PATCH_FUZZY_PRESENTATION_FAILED,
    PATCH_FUZZY_PRESENTATION_DIFFERENT,
    PATCH_FUZZY_PRESENTATION_WHITESPACE_CHANGED,
    PATCH_FUZZY_PRESENTATION_EQUIVALENT
} PatchSourceFuzzyPresentationResult;

/* Result must be zero on entry and stays zero on failure. Success owns backing
 * and comparable until this module releases them. Backing's raw slices borrow
 * the aligned documents and patch/source bytes; comparable owns its normalized
 * bytes and metadata. The caller must preflight lockstep before initialization. */
bool patch_source_fuzzy_presentation_init(
    PatchSourceFuzzyPresentation *result,
    const PatchDocument *original,
    const PatchSourceFuzzyDocument *corrected
);
PatchSourceFuzzyPresentationResult patch_source_fuzzy_presentation_check(
    const PatchSourceFuzzyPresentation *presentation,
    size_t deletion_offset,
    size_t deletion_count
);
void patch_source_fuzzy_presentation_release(PatchSourceFuzzyPresentation *presentation);

#endif
