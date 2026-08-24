#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_SEMANTIC_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_SEMANTIC_H

#include "executor_worker_patch_parser.h"

typedef struct PatchSourceFuzzySemanticToken {
    size_t start;
    size_t end;
} PatchSourceFuzzySemanticToken;

typedef enum PatchSourceFuzzySemanticResult {
    PATCH_SEMANTIC_FAILED,
    PATCH_SEMANTIC_END,
    PATCH_SEMANTIC_TOKEN
} PatchSourceFuzzySemanticResult;

/* Input length is at most PTRDIFF_MAX. Start cursor at zero and only reuse a
 * cursor returned with TOKEN; TOKEN writes one borrowed input range. END means
 * input was consumed. FAILED leaves both cursor and token unspecified. */
PatchSourceFuzzySemanticResult patch_source_fuzzy_semantic_next(
    PatchSlice input, size_t *cursor, PatchSourceFuzzySemanticToken *token
);

#endif
