#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_RAW_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_SOURCE_FUZZY_RAW_H

#include "executor_worker_patch_parser.h"

#include <stdint.h>

/* Both helpers allocate nothing. A NULL output/boundaries pointer measures;
 * a non-NULL pointer must provide exactly the capacity just measured from the
 * same immutable input. Successful joins contain valid UTF-8, and grapheme
 * boundaries contain count + 1 offsets from zero through length. */
bool patch_source_fuzzy_raw_join(
    uint8_t *output,
    size_t *output_length,
    const PatchSlice *segments,
    size_t segment_count
);
bool patch_source_fuzzy_raw_graphemes(
    const uint8_t *valid_utf8,
    size_t length,
    size_t *boundaries,
    size_t *grapheme_count
);
bool patch_source_fuzzy_raw_is_space(int32_t codepoint);

#endif
