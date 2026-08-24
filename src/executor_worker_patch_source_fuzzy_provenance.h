#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_PROVENANCE_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_PROVENANCE_H

#include "executor_worker_patch_source_fuzzy_document.h"
#include "executor_worker_patch_source.h"

typedef struct PatchSourceFuzzyProvenance {
    uint8_t *backing;
} PatchSourceFuzzyProvenance;

typedef enum PatchSourceFuzzyProvenanceResult {
    PATCH_SOURCE_FUZZY_PROVENANCE_FAILED,
    PATCH_SOURCE_FUZZY_PROVENANCE_REJECTED,
    PATCH_SOURCE_FUZZY_PROVENANCE_SUCCEEDED
} PatchSourceFuzzyProvenanceResult;

/* Result must be zero on entry. FAILED and REJECTED leave both result and
 * corrected unchanged. On success, corrected additions may borrow backing,
 * which must remain alive through formatting. Sources are borrowed only
 * during the call and must cover every non-null original old path. */
PatchSourceFuzzyProvenanceResult patch_source_fuzzy_provenance_reconcile(
    PatchSourceFuzzyProvenance *result,
    const PatchDocument *original,
    PatchSourceFuzzyDocument *corrected,
    const PatchSourceSet *sources
);
void patch_source_fuzzy_provenance_release(
    PatchSourceFuzzyProvenance *provenance
);

#endif
