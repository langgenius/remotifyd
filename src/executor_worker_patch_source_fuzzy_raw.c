#include "executor_worker_patch_source_fuzzy_raw.h"

#include <stdint.h>
#include <string.h>
#include <utf8proc.h>

bool patch_source_fuzzy_raw_is_space(int32_t codepoint) {
    utf8proc_category_t category;

    if ((codepoint >= 0x09 && codepoint <= 0x0d) || codepoint == 0x85) {
        return true;
    }
    category = utf8proc_category(codepoint);
    return category == UTF8PROC_CATEGORY_ZS ||
        category == UTF8PROC_CATEGORY_ZL || category == UTF8PROC_CATEGORY_ZP;
}

bool patch_source_fuzzy_raw_join(
    uint8_t *output,
    size_t *output_length,
    const PatchSlice *segments,
    size_t segment_count
) {
    static const uint8_t replacement[] = {0xef, 0xbf, 0xbd};
    size_t length = 0;
    size_t segment_index;

    for (segment_index = 0; segment_index < segment_count; segment_index++) {
        size_t cursor = 0;

        if (segment_index != 0) {
            if (length == SIZE_MAX) return false;
            if (output != NULL) output[length] = '\n';
            length++;
        }
        while (cursor < segments[segment_index].length) {
            utf8proc_int32_t codepoint;
            size_t remaining = segments[segment_index].length - cursor;
            utf8proc_ssize_t read = utf8proc_iterate(
                segments[segment_index].bytes + cursor,
                (utf8proc_ssize_t)(remaining > 4 ? 4 : remaining),
                &codepoint
            );
            size_t written = read > 0 ? (size_t)read : sizeof(replacement);

            if (written > SIZE_MAX - length) return false;
            if (output != NULL) {
                memcpy(
                    output + length,
                    read > 0 ? segments[segment_index].bytes + cursor : replacement,
                    written
                );
            }
            length += written;
            cursor += read > 0 ? (size_t)read : 1;
        }
    }
    *output_length = length;
    return true;
}

bool patch_source_fuzzy_raw_graphemes(
    const uint8_t *valid_utf8,
    size_t length,
    size_t *boundaries,
    size_t *grapheme_count
) {
    utf8proc_int32_t previous;
    utf8proc_int32_t state = 0;
    size_t cursor = 0;
    size_t count = 0;

    if (length > (size_t)PTRDIFF_MAX) return false;
    if (boundaries != NULL) boundaries[0] = 0;
    if (length != 0) {
        utf8proc_ssize_t read = utf8proc_iterate(
            valid_utf8, (utf8proc_ssize_t)length, &previous
        );
        if (read < 0) return false;
        cursor = (size_t)read;
        count = 1;
    }
    while (cursor < length) {
        utf8proc_int32_t next;
        utf8proc_ssize_t read = utf8proc_iterate(
            valid_utf8 + cursor,
            (utf8proc_ssize_t)(length - cursor),
            &next
        );
        if (read < 0) return false;
        if (utf8proc_grapheme_break_stateful(previous, next, &state)) {
            if (boundaries != NULL) boundaries[count] = cursor;
            count++;
            state = 0;
        }
        previous = next;
        cursor += (size_t)read;
    }
    if (boundaries != NULL) boundaries[count] = length;
    *grapheme_count = count;
    return true;
}
