#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_EVIDENCE_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_EVIDENCE_H
#include "executor_worker_patch_parser.h"
/* Entries borrow expected. NULL entries measures the exact count; success
 * alone writes count. A failed fill may have partial entries which the caller
 * must ignore. */
bool patch_source_fuzzy_evidence_collect(
    PatchSlice *entries, size_t capacity, size_t *count,
    PatchSlice expected, PatchSlice actual
);
bool patch_source_fuzzy_evidence_contains(
    const PatchSlice *entries, size_t count, PatchSlice addition
);
/* The two hunks must be lockstep-aligned and [start,end) one change block.
 * Success writes preserved; failure leaves it unspecified. */
bool patch_source_fuzzy_evidence_sensitive_tokens_preserved(
    bool *preserved, const PatchHunk *original, const PatchHunk *corrected,
    size_t start, size_t end
);
typedef enum PatchSourceFuzzyAlignedEvidenceResult {
    PATCH_ALIGNED_EVIDENCE_FAILED,
    PATCH_ALIGNED_EVIDENCE_REJECTED,
    PATCH_ALIGNED_EVIDENCE_OK
} PatchSourceFuzzyAlignedEvidenceResult;
/* Hunks are lockstep and [start,end) is one change block. NULL entries
 * measures; only OK writes count, while other results may leave partial entries. */
PatchSourceFuzzyAlignedEvidenceResult patch_source_fuzzy_evidence_collect_aligned(
    PatchSlice *entries, size_t capacity, size_t *count,
    const PatchHunk *original, const PatchHunk *corrected,
    size_t start, size_t end
);
#endif
