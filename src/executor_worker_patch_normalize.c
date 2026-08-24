#include "executor_worker_patch_normalize.h"

#include <stdlib.h>
#include <string.h>

typedef struct PatchNormalizeSlice {
    const uint8_t *bytes;
    size_t length;
} PatchNormalizeSlice;

typedef struct PatchNormalizeLine {
    PatchNormalizeSlice content;
    size_t raw_length;
} PatchNormalizeLine;

typedef struct PatchNormalizeHunkHeader {
    size_t old_start;
    size_t old_count;
    size_t new_start;
    size_t new_count;
    PatchNormalizeSlice old_start_text;
    PatchNormalizeSlice new_start_text;
    PatchNormalizeSlice suffix;
} PatchNormalizeHunkHeader;

typedef struct PatchNormalizePass {
    uint8_t *output;
    size_t length;
    size_t capacity;
    uint8_t changes;
} PatchNormalizePass;

static bool patch_normalize_append(
    PatchNormalizePass *pass,
    const uint8_t *bytes,
    size_t length
) {
    if (length > SIZE_MAX - pass->length) return false;
    if (pass->output != NULL) {
        if (pass->length > pass->capacity ||
            length > pass->capacity - pass->length) return false;
        if (length != 0) memcpy(pass->output + pass->length, bytes, length);
    }
    pass->length += length;
    return true;
}

static bool patch_normalize_append_byte(PatchNormalizePass *pass, uint8_t byte) {
    return patch_normalize_append(pass, &byte, 1);
}

static bool patch_normalize_append_number(PatchNormalizePass *pass, size_t number) {
    uint8_t bytes[3 * sizeof(size_t)];
    size_t cursor = sizeof(bytes);

    do {
        bytes[--cursor] = (uint8_t)('0' + number % 10);
        number /= 10;
    } while (number != 0);
    return patch_normalize_append(pass, bytes + cursor, sizeof(bytes) - cursor);
}

static PatchNormalizeSlice patch_normalize_line_ending(PatchNormalizeLine line) {
    PatchNormalizeSlice ending = {
        .bytes = line.content.bytes + line.content.length,
        .length = line.raw_length - line.content.length
    };

    return ending;
}

static bool patch_normalize_append_raw_line(
    PatchNormalizePass *pass,
    PatchNormalizeLine line
) {
    return patch_normalize_append(pass, line.content.bytes, line.raw_length);
}

static bool patch_normalize_slice_starts(
    PatchNormalizeSlice slice,
    const char *prefix
) {
    size_t length = 0;

    while (prefix[length] != '\0') length++;
    return slice.length >= length &&
        (length == 0 || memcmp(slice.bytes, prefix, length) == 0);
}

static bool patch_normalize_next_line(
    const uint8_t *bytes,
    size_t length,
    size_t *cursor,
    PatchNormalizeLine *line
) {
    size_t start;
    size_t content_end;

    if (*cursor >= length) return false;
    start = *cursor;
    while (*cursor < length && bytes[*cursor] != '\n') (*cursor)++;
    content_end = *cursor;
    if (*cursor < length) (*cursor)++;
    if (content_end > start && bytes[content_end - 1] == '\r') content_end--;
    line->content.bytes = bytes + start;
    line->content.length = content_end - start;
    line->raw_length = *cursor - start;
    return true;
}

static bool patch_normalize_parse_number(
    const uint8_t **cursor,
    const uint8_t *end,
    size_t *number
) {
    const uint8_t *start = *cursor;
    size_t value = 0;

    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        size_t digit = (size_t)(**cursor - '0');

        if (value > (SIZE_MAX - digit) / 10) return false;
        value = value * 10 + digit;
        (*cursor)++;
    }
    if (*cursor == start) return false;
    *number = value;
    return true;
}

static bool patch_normalize_parse_hunk_header(
    PatchNormalizeSlice line,
    PatchNormalizeHunkHeader *header
) {
    const uint8_t *cursor;
    const uint8_t *end;

    if (!patch_normalize_slice_starts(line, "@@ -")) return false;
    cursor = line.bytes + 4;
    end = line.bytes + line.length;
    header->old_count = 1;
    header->new_count = 1;
    header->old_start_text.bytes = cursor;
    if (!patch_normalize_parse_number(&cursor, end, &header->old_start)) return false;
    header->old_start_text.length = (size_t)(cursor - header->old_start_text.bytes);
    if (cursor < end && *cursor == ',') {
        cursor++;
        if (!patch_normalize_parse_number(&cursor, end, &header->old_count)) return false;
    }
    if (cursor >= end || *cursor++ != ' ' || cursor >= end || *cursor++ != '+') return false;
    header->new_start_text.bytes = cursor;
    if (!patch_normalize_parse_number(&cursor, end, &header->new_start)) return false;
    header->new_start_text.length = (size_t)(cursor - header->new_start_text.bytes);
    if (cursor < end && *cursor == ',') {
        cursor++;
        if (!patch_normalize_parse_number(&cursor, end, &header->new_count)) return false;
    }
    if ((size_t)(end - cursor) < 3 || memcmp(cursor, " @@", 3) != 0) return false;
    cursor += 3;
    header->suffix.bytes = cursor;
    header->suffix.length = (size_t)(end - cursor);
    return true;
}

static bool patch_normalize_is_hunk_header(PatchNormalizeSlice line) {
    PatchNormalizeHunkHeader ignored = {0};

    return patch_normalize_parse_hunk_header(line, &ignored);
}

static bool patch_normalize_read_file_header_pair(
    const uint8_t *bytes,
    size_t length,
    size_t *cursor,
    PatchNormalizeSlice line,
    PatchNormalizeLine *target
) {
    return patch_normalize_slice_starts(line, "--- ") &&
        patch_normalize_next_line(bytes, length, cursor, target) &&
        patch_normalize_slice_starts(target->content, "+++ ");
}

static bool patch_normalize_is_file_header(
    const uint8_t *bytes,
    size_t length,
    size_t cursor,
    PatchNormalizeSlice line
) {
    PatchNormalizeLine target;
    PatchNormalizeLine hunk;

    return patch_normalize_read_file_header_pair(
        bytes, length, &cursor, line, &target
    ) && patch_normalize_next_line(bytes, length, &cursor, &hunk) &&
        patch_normalize_is_hunk_header(hunk.content);
}

static bool patch_normalize_is_metadata(PatchNormalizeSlice line) {
    return patch_normalize_slice_starts(line, "diff --git ") ||
        patch_normalize_slice_starts(line, "Index: ");
}

static bool patch_normalize_is_hunk_boundary(
    const uint8_t *bytes,
    size_t length,
    size_t cursor,
    PatchNormalizeSlice line
) {
    if (line.length == 0) {
        PatchNormalizeLine next;

        if (!patch_normalize_next_line(bytes, length, &cursor, &next)) return false;
        return patch_normalize_is_hunk_header(next.content) ||
            patch_normalize_is_file_header(bytes, length, cursor, next.content);
    }
    return patch_normalize_is_hunk_header(line) ||
        patch_normalize_is_metadata(line) ||
        patch_normalize_is_file_header(bytes, length, cursor, line);
}

static bool patch_normalize_is_null_header(
    PatchNormalizeSlice line,
    const char *prefix
) {
    size_t prefix_length = 0;

    while (prefix[prefix_length] != '\0') prefix_length++;
    if (line.length < prefix_length + 9 ||
        memcmp(line.bytes, prefix, prefix_length) != 0 ||
        memcmp(line.bytes + prefix_length, "/dev/null", 9) != 0) return false;
    return line.length == prefix_length + 9 ||
        line.bytes[prefix_length + 9] == '\t';
}

typedef struct PatchNormalizeHunkBody {
    size_t old_count;
    size_t new_count;
    size_t end_cursor;
    bool file_pair_seen;
    bool old_null;
    bool new_null;
} PatchNormalizeHunkBody;

static void patch_normalize_count_operation(
    PatchNormalizeHunkBody *body,
    uint8_t operation
) {
    if (operation != '+') body->old_count++;
    if (operation != '-') body->new_count++;
}

static void patch_normalize_scan_hunk_body(
    const uint8_t *bytes,
    size_t length,
    size_t cursor,
    PatchNormalizeHunkBody *body
) {
    PatchNormalizeLine line;

    while (cursor < length) {
        size_t line_cursor = cursor;
        size_t pair_cursor;
        PatchNormalizeLine target;

        if (!patch_normalize_next_line(bytes, length, &cursor, &line)) break;
        if (patch_normalize_is_hunk_boundary(
            bytes, length, cursor, line.content
        )) {
            cursor = line_cursor;
            break;
        }
        pair_cursor = cursor;
        if (patch_normalize_read_file_header_pair(
            bytes, length, &pair_cursor, line.content, &target
        )) {
            body->file_pair_seen = true;
            body->old_null = patch_normalize_is_null_header(line.content, "--- ");
            body->new_null = patch_normalize_is_null_header(target.content, "+++ ");
        }
        if (line.content.length == 0) {
            patch_normalize_count_operation(body, ' ');
        } else if (line.content.bytes[0] == ' ' ||
            line.content.bytes[0] == '+' || line.content.bytes[0] == '-') {
            patch_normalize_count_operation(body, line.content.bytes[0]);
        }
    }
    body->end_cursor = cursor;
}

static bool patch_normalize_append_hunk_header(
    PatchNormalizePass *pass,
    PatchNormalizeLine line,
    PatchNormalizeHunkHeader header,
    PatchNormalizeHunkBody body,
    bool old_null,
    bool new_null,
    bool first_hunk
) {
    static const uint8_t prefix[] = "@@ -";
    static const uint8_t separator[] = " +";
    static const uint8_t close[] = " @@";
    PatchNormalizeSlice ending = patch_normalize_line_ending(line);
    bool counts_changed = header.old_count != body.old_count ||
        header.new_count != body.new_count;
    bool null_start_changed = false;

    if (first_hunk && old_null && body.old_count == 0 && body.new_count > 0 &&
        (header.old_start != 0 || header.new_start != 1)) null_start_changed = true;
    if (first_hunk && new_null && body.new_count == 0 && body.old_count > 0 &&
        (header.old_start != 1 || header.new_start != 0)) null_start_changed = true;
    if (!counts_changed && !null_start_changed) {
        return patch_normalize_append_raw_line(pass, line);
    }

    if (counts_changed) pass->changes |= PATCH_NORMALIZED_HUNK_COUNTS;
    if (null_start_changed) pass->changes |= PATCH_NORMALIZED_NULL_START;
    if (!patch_normalize_append(pass, prefix, sizeof(prefix) - 1)) return false;
    if (null_start_changed) {
        if (!patch_normalize_append_number(pass, old_null ? 0 : 1)) return false;
    } else if (!patch_normalize_append(
        pass, header.old_start_text.bytes, header.old_start_text.length
    )) return false;
    if (!patch_normalize_append_byte(pass, ',') ||
        !patch_normalize_append_number(pass, body.old_count) ||
        !patch_normalize_append(pass, separator, sizeof(separator) - 1)) return false;
    if (null_start_changed) {
        if (!patch_normalize_append_number(pass, new_null ? 0 : 1)) return false;
    } else if (!patch_normalize_append(
        pass, header.new_start_text.bytes, header.new_start_text.length
    )) return false;
    return patch_normalize_append_byte(pass, ',') &&
        patch_normalize_append_number(pass, body.new_count) &&
        patch_normalize_append(pass, close, sizeof(close) - 1) &&
        patch_normalize_append(pass, header.suffix.bytes, header.suffix.length) &&
        patch_normalize_append(pass, ending.bytes, ending.length);
}

static bool patch_normalize_append_hunk_body(
    PatchNormalizePass *pass,
    const uint8_t *bytes,
    size_t length,
    size_t start_cursor,
    size_t end_cursor
) {
    PatchNormalizeLine line;

    while (start_cursor < end_cursor) {
        PatchNormalizeSlice ending;

        if (!patch_normalize_next_line(bytes, length, &start_cursor, &line) ||
            start_cursor > end_cursor) return false;
        if (line.content.length != 0) {
            if (!patch_normalize_append_raw_line(pass, line)) return false;
            continue;
        }
        ending = patch_normalize_line_ending(line);
        pass->changes |= PATCH_NORMALIZED_EMPTY_CONTEXT;
        if (!patch_normalize_append_byte(pass, ' ') ||
            !patch_normalize_append(pass, ending.bytes, ending.length)) return false;
    }
    return start_cursor == end_cursor;
}

static bool patch_normalize_walk(
    PatchNormalizePass *pass,
    const uint8_t *bytes,
    size_t length
) {
    size_t cursor = 0;
    bool old_null = false;
    bool new_null = false;
    bool seen_hunk = false;
    PatchNormalizeLine line;

    while (patch_normalize_next_line(bytes, length, &cursor, &line)) {
        PatchNormalizeHunkHeader header = {0};
        PatchNormalizeLine target;
        size_t pair_cursor = cursor;

        /* A failed pair lookahead must not consume the following line. */
        if (patch_normalize_read_file_header_pair(
            bytes, length, &pair_cursor, line.content, &target
        )) {
            old_null = patch_normalize_is_null_header(line.content, "--- ");
            new_null = patch_normalize_is_null_header(target.content, "+++ ");
            seen_hunk = false;
            if (!patch_normalize_append_raw_line(pass, line) ||
                !patch_normalize_append_raw_line(pass, target)) return false;
            cursor = pair_cursor;
            continue;
        }

        if (patch_normalize_parse_hunk_header(line.content, &header)) {
            PatchNormalizeHunkBody body = {0};
            size_t body_cursor = cursor;
            bool first_hunk = !seen_hunk;

            patch_normalize_scan_hunk_body(bytes, length, body_cursor, &body);
            if (!patch_normalize_append_hunk_header(
                pass, line, header, body, old_null, new_null, first_hunk
            ) || !patch_normalize_append_hunk_body(
                pass, bytes, length, body_cursor, body.end_cursor
            )) return false;
            seen_hunk = true;
            if (body.file_pair_seen) {
                old_null = body.old_null;
                new_null = body.new_null;
                seen_hunk = false;
            }
            cursor = body.end_cursor;
            continue;
        }

        if (!patch_normalize_append_raw_line(pass, line)) return false;
    }

    if (length != 0 && bytes[length - 1] != '\n') {
        pass->changes |= PATCH_NORMALIZED_TRAILING_LF;
        if (!patch_normalize_append_byte(pass, '\n')) return false;
    }
    return true;
}

bool patch_normalize(
    PatchNormalizedText *result,
    const uint8_t *bytes,
    size_t length
) {
    PatchNormalizedText normalized = {0};
    PatchNormalizePass measure = {0};
    PatchNormalizePass write = {0};
    uint8_t *output;

    if (!patch_normalize_walk(&measure, bytes, length)) return false;
    if (measure.changes == 0) {
        if (measure.length != length) return false;
        normalized.bytes = bytes;
        normalized.length = length;
        *result = normalized;
        return true;
    }

    output = malloc(measure.length);
    if (output == NULL) return false;
    write.output = output;
    write.capacity = measure.length;
    if (!patch_normalize_walk(&write, bytes, length) ||
        write.length != measure.length || write.changes != measure.changes) {
        free(output);
        return false;
    }

    normalized.bytes = output;
    normalized.length = write.length;
    normalized.changes = write.changes;
    *result = normalized;
    return true;
}

void patch_normalized_text_release(PatchNormalizedText *result) {
    if (result->changes != 0) free((void *)result->bytes);
    memset(result, 0, sizeof(*result));
}
