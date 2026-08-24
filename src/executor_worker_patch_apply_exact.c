#include "executor_worker_patch_apply_exact.h"

#include <stdlib.h>
#include <string.h>

typedef struct PatchSourceLines {
    PatchSlice *lines;
    size_t count;
} PatchSourceLines;

typedef enum PatchLineConversion {
    PATCH_LINE_KEEP,
    PATCH_LINE_ADD_CR,
    PATCH_LINE_REMOVE_CR
} PatchLineConversion;

typedef struct PatchApplyPass {
    uint8_t *output;
    size_t length;
    size_t capacity;
    size_t line_count;
} PatchApplyPass;

static bool patch_line_adds_cr(
    const PatchLine *line,
    PatchLineConversion conversion
) {
    return (conversion == PATCH_LINE_KEEP && line->crlf) ||
        (conversion == PATCH_LINE_ADD_CR && !line->crlf && !line->no_final_newline);
}

static PatchLineConversion patch_line_conversion(
    const uint8_t *source,
    size_t source_length,
    const PatchFile *file
) {
    bool source_has_crlf = false;
    bool source_has_lone_lf = false;
    bool patch_has_crlf = false;
    bool patch_all_crlf = true;
    size_t cursor;
    size_t hunk_index;

    for (cursor = 0; cursor < source_length; cursor++) {
        if (source[cursor] != '\n') continue;
        if (cursor != 0 && source[cursor - 1] == '\r') source_has_crlf = true;
        else source_has_lone_lf = true;
    }
    for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
        const PatchHunk *hunk = &file->hunks[hunk_index];
        size_t line_index;

        for (line_index = 0; line_index < hunk->line_count; line_index++) {
            const PatchLine *line = &hunk->lines[line_index];

            if (line->crlf) patch_has_crlf = true;
            else if (!line->no_final_newline) patch_all_crlf = false;
        }
    }
    if (source_has_crlf && !source_has_lone_lf && !patch_has_crlf) {
        return PATCH_LINE_ADD_CR;
    }
    if (!source_has_crlf && source_has_lone_lf &&
        patch_has_crlf && patch_all_crlf) return PATCH_LINE_REMOVE_CR;
    return PATCH_LINE_KEEP;
}

static bool patch_source_lines_build(
    PatchSourceLines *result,
    const uint8_t *source,
    size_t source_length
) {
    PatchSourceLines lines = {0};
    size_t count = 1;
    size_t start = 0;
    size_t cursor;
    size_t index = 0;

    for (cursor = 0; cursor < source_length; cursor++) {
        if (source[cursor] == '\n') {
            if (count == SIZE_MAX) return false;
            count++;
        }
    }
    if (count > SIZE_MAX / sizeof(*lines.lines)) return false;
    lines.lines = malloc(count * sizeof(*lines.lines));
    if (lines.lines == NULL) return false;
    lines.count = count;

    for (cursor = 0; cursor < source_length; cursor++) {
        if (source[cursor] != '\n') continue;
        lines.lines[index].bytes = source + start;
        lines.lines[index].length = cursor - start;
        index++;
        start = cursor + 1;
    }
    lines.lines[index].bytes = source_length == 0 ? source : source + start;
    lines.lines[index].length = source_length - start;
    *result = lines;
    return true;
}

static void patch_source_lines_release(PatchSourceLines *lines) {
    free(lines->lines);
    memset(lines, 0, sizeof(*lines));
}

static PatchExactResult patch_source_lines_prepare_eof(
    const PatchSourceLines *lines,
    const PatchFile *file,
    size_t *effective_count
) {
    bool remove_eof_newline = false;
    bool add_eof_newline = false;
    size_t count = lines->count;

    if (file->hunk_count != 0) {
        const PatchHunk *last = &file->hunks[file->hunk_count - 1];
        size_t index;

        for (index = 0; index < last->line_count; index++) {
            const PatchLine *line = &last->lines[index];

            if (!line->no_final_newline) continue;
            if (line->operation == '+') remove_eof_newline = true;
            else if (line->operation == '-') add_eof_newline = true;
        }
    }
    if (remove_eof_newline && add_eof_newline) {
        if (lines->lines[count - 1].length == 0) return PATCH_EXACT_MISMATCH;
    } else if (remove_eof_newline) {
        if (lines->lines[count - 1].length != 0) return PATCH_EXACT_MISMATCH;
        count--;
    } else if (add_eof_newline) {
        if (lines->lines[count - 1].length == 0) return PATCH_EXACT_MISMATCH;
        if (count == SIZE_MAX) return PATCH_EXACT_FAILED;
        count++;
    }
    *effective_count = count;
    return PATCH_EXACT_SUCCEEDED;
}

static PatchSlice patch_source_line_at(const PatchSourceLines *lines, size_t index) {
    PatchSlice empty = {0};

    return index == lines->count ? empty : lines->lines[index];
}

static bool patch_line_matches(
    PatchSlice source,
    const PatchLine *line,
    PatchLineConversion conversion
) {
    bool add_cr = patch_line_adds_cr(line, conversion);
    size_t expected_length = line->content.length;

    if (add_cr) {
        if (expected_length == SIZE_MAX) return false;
        expected_length++;
    }
    if (source.length != expected_length) return false;
    if (line->content.length != 0 &&
        memcmp(source.bytes, line->content.bytes, line->content.length) != 0) return false;
    return !add_cr || source.bytes[line->content.length] == '\r';
}

static bool patch_hunk_matches(
    const PatchSourceLines *lines,
    size_t effective_count,
    const PatchHunk *hunk,
    size_t position,
    PatchLineConversion conversion,
    size_t *next_source
) {
    size_t source_index = position;
    size_t trailing_context = 0;
    size_t index;

    for (index = 0; index < hunk->line_count; index++) {
        const PatchLine *line = &hunk->lines[index];

        if (line->operation == '+') {
            trailing_context = 0;
            continue;
        }
        if (line->operation != '-' && line->operation != ' ') return false;
        if (source_index >= effective_count ||
            !patch_line_matches(
                patch_source_line_at(lines, source_index), line, conversion
            )) return false;
        source_index++;
        if (line->operation == ' ') trailing_context++;
        else trailing_context = 0;
    }
    *next_source = source_index - trailing_context;
    return true;
}

static bool patch_find_hunk(
    const PatchSourceLines *lines,
    size_t effective_count,
    const PatchHunk *hunk,
    int64_t requested,
    size_t minimum,
    PatchLineConversion conversion,
    size_t *position,
    size_t *next_source
) {
    size_t maximum;
    size_t candidate;

    if (hunk->old_length > effective_count) return false;
    maximum = effective_count - hunk->old_length;
    if (maximum < minimum) return false;

    if (requested < (int64_t)minimum) {
        for (candidate = minimum;; candidate++) {
            if (patch_hunk_matches(
                lines, effective_count, hunk, candidate, conversion, next_source
            )) {
                *position = candidate;
                return true;
            }
            if (candidate == maximum) break;
        }
        return false;
    }
    if ((uint64_t)requested > (uint64_t)maximum) {
        for (candidate = maximum;; candidate--) {
            if (patch_hunk_matches(
                lines, effective_count, hunk, candidate, conversion, next_source
            )) {
                *position = candidate;
                return true;
            }
            if (candidate == minimum) break;
        }
        return false;
    }

    candidate = (size_t)requested;
    if (patch_hunk_matches(
        lines, effective_count, hunk, candidate, conversion, next_source
    )) {
        *position = candidate;
        return true;
    }
    {
        size_t distance;

        for (distance = 1;; distance++) {
            bool searched = false;

            if (distance <= maximum - candidate) {
                searched = true;
                if (patch_hunk_matches(
                    lines, effective_count, hunk, candidate + distance,
                    conversion, next_source
                )) {
                    *position = candidate + distance;
                    return true;
                }
            }
            if (distance <= candidate - minimum) {
                searched = true;
                if (patch_hunk_matches(
                    lines, effective_count, hunk, candidate - distance,
                    conversion, next_source
                )) {
                    *position = candidate - distance;
                    return true;
                }
            }
            if (!searched) break;
        }
    }
    return false;
}

static bool patch_apply_append(
    PatchApplyPass *pass,
    const uint8_t *bytes,
    size_t length
) {
    if (length > SIZE_MAX - pass->length) return false;
    if (pass->output != NULL) {
        if (length > pass->capacity - pass->length) return false;
        if (length != 0) memcpy(pass->output + pass->length, bytes, length);
    }
    pass->length += length;
    return true;
}

static bool patch_apply_begin_line(PatchApplyPass *pass) {
    static const uint8_t lf = '\n';

    if (pass->line_count == SIZE_MAX) return false;
    if (pass->line_count != 0 && !patch_apply_append(pass, &lf, 1)) return false;
    pass->line_count++;
    return true;
}

static bool patch_apply_source_line(PatchApplyPass *pass, PatchSlice line) {
    return patch_apply_begin_line(pass) &&
        patch_apply_append(pass, line.bytes, line.length);
}

static bool patch_apply_patch_line(
    PatchApplyPass *pass,
    const PatchLine *line,
    PatchLineConversion conversion
) {
    static const uint8_t cr = '\r';

    if (!patch_apply_begin_line(pass) ||
        !patch_apply_append(pass, line->content.bytes, line->content.length)) return false;
    return !patch_line_adds_cr(line, conversion) ||
        patch_apply_append(pass, &cr, 1);
}

static bool patch_apply_source_range(
    PatchApplyPass *pass,
    const PatchSourceLines *lines,
    size_t start,
    size_t end
) {
    size_t index;

    for (index = start; index < end; index++) {
        if (!patch_apply_source_line(pass, patch_source_line_at(lines, index))) return false;
    }
    return true;
}

static bool patch_apply_hunk_lines(
    PatchApplyPass *pass,
    const PatchSourceLines *lines,
    const PatchHunk *hunk,
    size_t position,
    PatchLineConversion conversion
) {
    size_t emit_count = hunk->line_count;
    size_t source_index = position;
    size_t index;

    while (emit_count != 0 && hunk->lines[emit_count - 1].operation == ' ') emit_count--;
    for (index = 0; index < emit_count; index++) {
        const PatchLine *line = &hunk->lines[index];

        if (line->operation == '+') {
            if (!patch_apply_patch_line(pass, line, conversion)) return false;
        } else if (line->operation == '-') {
            source_index++;
        } else if (line->operation == ' ') {
            if (!patch_apply_source_line(
                pass, patch_source_line_at(lines, source_index)
            )) return false;
            source_index++;
        } else {
            return false;
        }
    }
    return true;
}

static bool patch_hunk_matchable(
    const PatchHunk *hunk,
    PatchLineConversion conversion
) {
    size_t index;

    for (index = 0; index < hunk->line_count; index++) {
        const PatchLine *line = &hunk->lines[index];

        if (line->operation != '+' && line->operation != '-' &&
            line->operation != ' ') return false;
        if (patch_line_adds_cr(line, conversion) &&
            line->content.length == SIZE_MAX) return false;
    }
    return true;
}

static PatchExactResult patch_file_apply_pass(
    PatchApplyPass *pass,
    const PatchSourceLines *lines,
    size_t effective_count,
    const PatchFile *file,
    PatchLineConversion conversion
) {
    int64_t previous_offset = 0;
    size_t minimum = 0;
    size_t index;

    if (effective_count >= (size_t)INT64_MAX) return PATCH_EXACT_FAILED;
    for (index = 0; index < file->hunk_count; index++) {
        const PatchHunk *hunk = &file->hunks[index];
        int64_t requested;
        int64_t start;
        size_t position;
        size_t next_source;

        if (!patch_hunk_matchable(hunk, conversion)) return PATCH_EXACT_FAILED;
        if (hunk->old_start > (size_t)INT64_MAX) return PATCH_EXACT_FAILED;
        start = (int64_t)hunk->old_start - 1;
        if ((previous_offset > 0 && start > INT64_MAX - previous_offset) ||
            (previous_offset < 0 && start < INT64_MIN - previous_offset)) {
            return PATCH_EXACT_FAILED;
        }
        requested = start + previous_offset;
        if (!patch_find_hunk(
            lines, effective_count, hunk, requested, minimum, conversion,
            &position, &next_source
        )) return PATCH_EXACT_MISMATCH;
        if (!patch_apply_source_range(pass, lines, minimum, position) ||
            !patch_apply_hunk_lines(pass, lines, hunk, position, conversion)) {
            return PATCH_EXACT_FAILED;
        }
        minimum = next_source;
        previous_offset = (int64_t)(position + 1) - (int64_t)hunk->old_start;
    }
    return patch_apply_source_range(pass, lines, minimum, effective_count)
        ? PATCH_EXACT_SUCCEEDED : PATCH_EXACT_FAILED;
}

PatchExactResult patch_file_apply_exact(
    PatchAppliedText *result,
    const uint8_t *source,
    size_t source_length,
    const PatchFile *file
) {
    PatchSourceLines lines = {0};
    PatchApplyPass measure = {0};
    PatchApplyPass write = {0};
    PatchAppliedText applied = {0};
    PatchLineConversion conversion;
    size_t effective_count;

    PatchExactResult pass_result;

    if (!patch_source_lines_build(&lines, source, source_length)) {
        return PATCH_EXACT_FAILED;
    }
    conversion = patch_line_conversion(source, source_length, file);
    pass_result = patch_source_lines_prepare_eof(&lines, file, &effective_count);
    if (pass_result != PATCH_EXACT_SUCCEEDED) {
        patch_source_lines_release(&lines);
        return pass_result;
    }
    pass_result = patch_file_apply_pass(
        &measure, &lines, effective_count, file, conversion
    );
    if (pass_result != PATCH_EXACT_SUCCEEDED) {
        patch_source_lines_release(&lines);
        return pass_result;
    }
    applied.bytes = malloc(measure.length == 0 ? 1 : measure.length);
    if (applied.bytes == NULL) {
        patch_source_lines_release(&lines);
        return PATCH_EXACT_FAILED;
    }
    write.output = applied.bytes;
    write.capacity = measure.length;
    if (patch_file_apply_pass(&write, &lines, effective_count, file, conversion) !=
            PATCH_EXACT_SUCCEEDED ||
        write.length != measure.length || write.line_count != measure.line_count) {
        free(applied.bytes);
        patch_source_lines_release(&lines);
        return PATCH_EXACT_FAILED;
    }
    applied.length = write.length;
    patch_source_lines_release(&lines);
    *result = applied;
    return PATCH_EXACT_SUCCEEDED;
}

void patch_applied_text_release(PatchAppliedText *result) {
    free(result->bytes);
    memset(result, 0, sizeof(*result));
}
