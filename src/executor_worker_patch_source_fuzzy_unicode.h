#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_UNICODE_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_UNICODE_H

#include "executor_worker_patch_parser.h"

typedef struct PatchSourceFuzzyLine {
    /* Comparable contains valid UTF-8 in the batch bytes allocation.
     * Boundaries contains grapheme_count + 1 offsets relative to comparable:
     * first 0 and last comparable.length. An empty line has one offset 0. */
    PatchSlice comparable;
    const size_t *boundaries;
    size_t grapheme_count;
} PatchSourceFuzzyLine;

typedef struct PatchSourceFuzzyLines {
    uint8_t *bytes;
    PatchSourceFuzzyLine *lines;
    size_t line_count;
} PatchSourceFuzzyLines;

/* Result must be zero on entry. Output lines correspond one-for-one with raw
 * input order and own at most two batch allocations. Failure leaves it zero
 * and does not retain raw slices. A zero count succeeds with a zero resource
 * and allows raw to be NULL. */
bool patch_source_fuzzy_lines_build(
    PatchSourceFuzzyLines *result,
    const PatchSlice *raw,
    size_t count
);
void patch_source_fuzzy_lines_release(PatchSourceFuzzyLines *lines);

/* Maximum must be at most three. False means the distance exceeds it and
 * leaves distance unspecified. */
bool patch_source_fuzzy_bounded_distance(
    const PatchSourceFuzzyLine *expected,
    const PatchSourceFuzzyLine *actual,
    uint8_t maximum,
    uint8_t *distance
);
bool patch_source_fuzzy_strong_anchor(const PatchSourceFuzzyLine *line);

#endif
