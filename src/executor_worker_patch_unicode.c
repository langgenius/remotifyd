#include "executor_worker_patch_unicode.h"

#include <utf8proc.h>

bool patch_unicode_is_letter_number(int32_t codepoint) {
    utf8proc_category_t category;

    category = utf8proc_category(codepoint);
    return (category >= UTF8PROC_CATEGORY_LU &&
            category <= UTF8PROC_CATEGORY_LO) ||
        (category >= UTF8PROC_CATEGORY_ND &&
         category <= UTF8PROC_CATEGORY_NO);
}

bool patch_unicode_strong_escape_proof(
    const PatchSlice *segments,
    size_t segment_count
) {
    size_t characters = 0;
    size_t semantic = 0;
    size_t segment_index;

    for (segment_index = 0; segment_index < segment_count; segment_index++) {
        PatchSlice segment = segments[segment_index];
        size_t cursor = 0;

        if (segment_index != 0 && characters < 8) characters++;
        while (cursor < segment.length) {
            utf8proc_int32_t codepoint;
            utf8proc_ssize_t consumed = utf8proc_iterate(
                segment.bytes + cursor,
                (utf8proc_ssize_t)(segment.length - cursor),
                &codepoint
            );

            /* Go range replaces each invalid byte with U+FFFD and advances
             * once, so provenance strength stays identical on malformed UTF-8. */
            if (consumed < 0) {
                codepoint = 0xfffd;
                consumed = 1;
            }
            cursor += (size_t)consumed;
            if (characters < 8) characters++;
            if (semantic < 4 && (codepoint == '_' ||
                patch_unicode_is_letter_number(codepoint))) semantic++;
            if (characters >= 8 && semantic >= 4) return true;
        }
    }
    return false;
}
