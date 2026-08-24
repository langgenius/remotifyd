#include "executor_worker_patch_source_fuzzy_unicode.h"
#include "executor_worker_patch_source_fuzzy_raw.h"
#include "executor_worker_patch_unicode.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <utf8proc.h>

static const char *patch_source_fuzzy_punctuation(
    utf8proc_int32_t codepoint,
    size_t *length
) {
    const char *replacement = NULL;

    switch (codepoint) {
        case 0xff0c: case 0x3001: replacement = ","; break;
        case 0x3002: case 0x00b7: replacement = "."; break;
        case 0xff01: replacement = "!"; break;
        case 0xff1f: replacement = "?"; break;
        case 0xff1a: replacement = ":"; break;
        case 0xff1b: replacement = ";"; break;
        case 0xff08: replacement = "("; break;
        case 0xff09: replacement = ")"; break;
        case 0x3010: case 0xff3b: replacement = "["; break;
        case 0x3011: case 0xff3d: replacement = "]"; break;
        case 0xff5b: replacement = "{"; break;
        case 0xff5d: replacement = "}"; break;
        case 0x201c: case 0x201d: case 0x300c: case 0x300d:
        case 0x300e: case 0x300f: replacement = "\""; break;
        case 0x2018: case 0x2019: replacement = "'"; break;
        case 0x300a: case 0x3008: replacement = "<"; break;
        case 0x300b: case 0x3009: replacement = ">"; break;
        case 0x2014: case 0x2013: replacement = "-"; break;
        case 0x2026: replacement = "..."; break;
    }
    *length = replacement == NULL ? 0 : strlen(replacement);
    return replacement;
}

static bool patch_source_fuzzy_cluster(const uint8_t *bytes, size_t length,
    size_t start, size_t *end, utf8proc_int32_t *single) {
    utf8proc_int32_t previous;
    utf8proc_int32_t state = 0;
    utf8proc_ssize_t first = utf8proc_iterate(
        bytes + start, (utf8proc_ssize_t)(length - start), &previous
    );
    size_t cursor;

    if (first < 0) return false;
    cursor = start + (size_t)first;
    *single = previous;
    while (cursor < length) {
        utf8proc_int32_t next;
        utf8proc_ssize_t read = utf8proc_iterate(
            bytes + cursor, (utf8proc_ssize_t)(length - cursor), &next
        );
        if (read < 0) return false;
        if (utf8proc_grapheme_break_stateful(previous, next, &state)) break;
        *single = -1;
        previous = next;
        cursor += (size_t)read;
    }
    *end = cursor;
    return true;
}

static bool patch_source_fuzzy_line(
    uint8_t *bytes,
    size_t length,
    size_t *output_length
) {
    size_t input = 0;
    size_t output = 0;
    bool space_pending = false;

    while (input < length) {
        size_t cluster_end;
        utf8proc_int32_t single;
        const char *replacement;
        size_t replacement_length;

        if (!patch_source_fuzzy_cluster(
            bytes, length, input, &cluster_end, &single
        )) return false;
        replacement = patch_source_fuzzy_punctuation(
            single, &replacement_length
        );
        if (replacement != NULL) {
            if (space_pending) bytes[output++] = ' ';
            memmove(bytes + output, replacement, replacement_length);
            output += replacement_length;
            space_pending = false;
        } else {
            size_t cursor = input;
            while (cursor < cluster_end) {
                utf8proc_int32_t codepoint;
                utf8proc_ssize_t read = utf8proc_iterate(
                    bytes + cursor,
                    (utf8proc_ssize_t)(cluster_end - cursor),
                    &codepoint
                );
                if (read < 0) return false;
                if (patch_source_fuzzy_raw_is_space(codepoint)) {
                    space_pending = output != 0;
                } else {
                    if (space_pending) bytes[output++] = ' ';
                    memmove(bytes + output, bytes + cursor, (size_t)read);
                    output += (size_t)read;
                    space_pending = false;
                }
                cursor += (size_t)read;
            }
        }
        input = cluster_end;
    }
    *output_length = output;
    return true;
}

static bool patch_source_fuzzy_lines_compact(
    uint8_t *bytes,
    size_t length,
    size_t line_count,
    size_t *output_length
) {
    size_t input = 0;
    size_t output = 0;
    size_t index;

    for (index = 0; index < line_count; index++) {
        uint8_t *separator = memchr(bytes + input, '\n', length - input);
        size_t line_end = separator == NULL ? length : (size_t)(separator - bytes);
        size_t compacted;

        if ((index + 1 < line_count) != (separator != NULL)) return false;
        if (!patch_source_fuzzy_line(
            bytes + input, line_end - input, &compacted
        )) return false;
        memmove(bytes + output, bytes + input, compacted);
        output += compacted;
        input = line_end;
        if (separator != NULL) {
            bytes[output++] = '\n';
            input++;
        }
    }
    *output_length = output;
    return input == length;
}

static bool patch_source_fuzzy_metadata(
    uint8_t *bytes,
    size_t length,
    size_t line_count,
    PatchSourceFuzzyLine *lines,
    size_t *boundaries,
    size_t *boundary_count
) {
    size_t input = 0;
    size_t used = 0;
    size_t index;

    for (index = 0; index < line_count; index++) {
        uint8_t *separator = memchr(bytes + input, '\n', length - input);
        size_t line_end = separator == NULL ? length : (size_t)(separator - bytes);
        size_t grapheme_count;

        if ((index + 1 < line_count) != (separator != NULL) ||
            !patch_source_fuzzy_raw_graphemes(
                bytes + input,
                line_end - input,
                boundaries == NULL ? NULL : boundaries + used,
                &grapheme_count
            ) || grapheme_count == SIZE_MAX ||
            grapheme_count + 1 > SIZE_MAX - used) {
            return false;
        }
        if (lines != NULL) {
            lines[index].comparable.bytes = bytes + input;
            lines[index].comparable.length = line_end - input;
            lines[index].boundaries = boundaries + used;
            lines[index].grapheme_count = grapheme_count;
        }
        used += grapheme_count + 1;
        input = line_end + (separator == NULL ? 0 : 1);
    }
    *boundary_count = used;
    return input == length;
}

bool patch_source_fuzzy_lines_build(
    PatchSourceFuzzyLines *result,
    const PatchSlice *raw,
    size_t count
) {
    utf8proc_uint8_t *normalized = NULL;
    uint8_t *sanitized;
    size_t sanitized_length;
    size_t comparable_length;
    size_t boundary_count;
    size_t written_boundaries;
    size_t line_bytes;
    size_t boundary_bytes;
    PatchSourceFuzzyLine *lines;
    size_t *boundaries;
    size_t written;
    utf8proc_ssize_t normalized_length;

    if (count == 0) return true;
    if (!patch_source_fuzzy_raw_join(NULL, &sanitized_length, raw, count) ||
        sanitized_length > (size_t)PTRDIFF_MAX) {
        return false;
    }
    sanitized = malloc(sanitized_length == 0 ? 1 : sanitized_length);
    if (sanitized == NULL) return false;
    if (!patch_source_fuzzy_raw_join(sanitized, &written, raw, count) ||
        written != sanitized_length) {
        free(sanitized);
        return false;
    }

    /* Canonical LF boundaries keep NFKC composition local to each raw line. */
    normalized_length = utf8proc_map(
        sanitized,
        (utf8proc_ssize_t)sanitized_length,
        &normalized,
        UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_COMPAT
    );
    free(sanitized);
    if (normalized_length < 0) return false;
    if (!patch_source_fuzzy_lines_compact(
        normalized, (size_t)normalized_length, count, &comparable_length
    )) {
        free(normalized);
        return false;
    }
    if (!patch_source_fuzzy_metadata(
        normalized, comparable_length, count, NULL, NULL, &boundary_count
    ) || count > SIZE_MAX / sizeof(*lines) ||
        boundary_count > SIZE_MAX / sizeof(*boundaries)) {
        free(normalized);
        return false;
    }
    line_bytes = count * sizeof(*lines);
    boundary_bytes = boundary_count * sizeof(*boundaries);
    if (boundary_bytes > SIZE_MAX - line_bytes) {
        free(normalized);
        return false;
    }
    lines = malloc(line_bytes + boundary_bytes);
    if (lines == NULL) {
        free(normalized);
        return false;
    }
    boundaries = (size_t *)((uint8_t *)lines + line_bytes);
    if (!patch_source_fuzzy_metadata(
        normalized, comparable_length, count,
        lines, boundaries, &written_boundaries
    ) || written_boundaries != boundary_count) {
        free(lines);
        free(normalized);
        return false;
    }
    result->bytes = normalized;
    result->lines = lines;
    result->line_count = count;
    return true;
}

void patch_source_fuzzy_lines_release(PatchSourceFuzzyLines *lines) {
    free(lines->bytes);
    free(lines->lines);
    memset(lines, 0, sizeof(*lines));
}

static uint8_t patch_source_fuzzy_band_read(
    const uint8_t *band,
    size_t length,
    size_t start,
    size_t index,
    uint8_t outside
) {
    if (index < start || index - start >= length) return outside;
    return band[index - start];
}

static bool patch_source_fuzzy_grapheme_equal(
    const PatchSourceFuzzyLine *left,
    size_t left_index,
    const PatchSourceFuzzyLine *right,
    size_t right_index
) {
    size_t left_start = left->boundaries[left_index];
    size_t right_start = right->boundaries[right_index];
    size_t left_length = left->boundaries[left_index + 1] - left_start;
    size_t right_length = right->boundaries[right_index + 1] - right_start;

    return left_length == right_length && memcmp(
        left->comparable.bytes + left_start,
        right->comparable.bytes + right_start,
        left_length
    ) == 0;
}

bool patch_source_fuzzy_bounded_distance(
    const PatchSourceFuzzyLine *expected,
    const PatchSourceFuzzyLine *actual,
    uint8_t maximum,
    uint8_t *distance
) {
    uint8_t previous[7];
    uint8_t current[7];
    uint8_t outside = (uint8_t)(maximum + 1);
    size_t previous_start = 0;
    size_t previous_length = actual->grapheme_count < maximum
        ? actual->grapheme_count + 1 : (size_t)maximum + 1;
    size_t expected_index;
    size_t index;

    if (expected->grapheme_count > actual->grapheme_count + maximum ||
        actual->grapheme_count > expected->grapheme_count + maximum) {
        return false;
    }
    for (index = 0; index < previous_length; index++) previous[index] = (uint8_t)index;
    for (expected_index = 1;
        expected_index <= expected->grapheme_count;
        expected_index++) {
        size_t current_start = expected_index > maximum
            ? expected_index - maximum : 0;
        size_t current_end = expected_index < actual->grapheme_count
            ? expected_index : actual->grapheme_count;
        size_t current_length;
        uint8_t row_minimum = outside;
        size_t actual_index;

        if (actual->grapheme_count - current_end > maximum) {
            current_end += maximum;
        } else {
            current_end = actual->grapheme_count;
        }
        current_length = current_end - current_start + 1;
        for (actual_index = current_start;
            actual_index <= current_end;
            actual_index++) {
            uint8_t value;
            if (actual_index == 0) {
                value = (uint8_t)expected_index;
            } else {
                uint8_t deletion = (uint8_t)(patch_source_fuzzy_band_read(
                    previous, previous_length, previous_start,
                    actual_index, outside
                ) + 1);
                uint8_t insertion = (uint8_t)(patch_source_fuzzy_band_read(
                    current, actual_index - current_start,
                    current_start, actual_index - 1, outside
                ) + 1);
                uint8_t substitution = patch_source_fuzzy_band_read(
                    previous, previous_length, previous_start,
                    actual_index - 1, outside
                );
                if (!patch_source_fuzzy_grapheme_equal(
                    expected, expected_index - 1, actual, actual_index - 1
                )) substitution++;
                value = deletion < insertion ? deletion : insertion;
                if (substitution < value) value = substitution;
                if (outside < value) value = outside;
            }
            current[actual_index - current_start] = value;
            if (value < row_minimum) row_minimum = value;
        }
        if (row_minimum > maximum) return false;
        memcpy(previous, current, current_length);
        previous_start = current_start;
        previous_length = current_length;
    }
    *distance = patch_source_fuzzy_band_read(
        previous, previous_length, previous_start,
        actual->grapheme_count, outside
    );
    return *distance <= maximum;
}

bool patch_source_fuzzy_strong_anchor(const PatchSourceFuzzyLine *line) {
    size_t semantic = 0;
    size_t index;

    if (line->grapheme_count < 8) return false;
    for (index = 0; index < line->grapheme_count; index++) {
        size_t start = line->boundaries[index];
        size_t length = line->boundaries[index + 1] - start;
        utf8proc_int32_t codepoint;
        utf8proc_ssize_t read = utf8proc_iterate(
            line->comparable.bytes + start,
            (utf8proc_ssize_t)length,
            &codepoint
        );
        if (read == (utf8proc_ssize_t)length && (codepoint == '_' ||
            patch_unicode_is_letter_number(codepoint))) semantic++;
    }
    return semantic >= 4;
}
