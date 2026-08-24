#include "executor_worker_patch_source_fuzzy_presentation.h"

#include "executor_worker_patch_source_fuzzy_raw.h"

#include <stdlib.h>
#include <string.h>
#include <utf8proc.h>

typedef struct PatchPresentationBacking {
    size_t line_count;
    PatchSlice lines[];
} PatchPresentationBacking;

typedef struct PatchPresentationRunReader {
    const PatchSlice *segments;
    size_t segment_count, segment, cursor;
    bool seen_text, pending;
    size_t run_length;
    int32_t previous, state;
} PatchPresentationRunReader;

static bool patch_presentation_codepoint(PatchPresentationRunReader *reader,
    int32_t *codepoint) {
    while (reader->segment < reader->segment_count) {
        PatchSlice slice = reader->segments[reader->segment];
        if (reader->cursor < slice.length) {
            size_t remaining = slice.length - reader->cursor;
            utf8proc_ssize_t read = utf8proc_iterate(
                slice.bytes + reader->cursor,
                (utf8proc_ssize_t)(remaining > 4 ? 4 : remaining), codepoint
            );
            reader->cursor += read > 0 ? (size_t)read : 1;
            if (read < 0) *codepoint = 0xfffd;
            return true;
        }
        reader->segment++;
        reader->cursor = 0;
        if (reader->segment < reader->segment_count) {
            *codepoint = '\n';
            return true;
        }
    }
    return false;
}

static bool patch_presentation_next_run(
    PatchPresentationRunReader *reader,
    size_t *run_length
) {
    int32_t codepoint;
    while (patch_presentation_codepoint(reader, &codepoint)) {
        if (patch_source_fuzzy_raw_is_space(codepoint)) {
            if (!reader->seen_text) continue;
            if (!reader->pending) {
                reader->pending = true;
                reader->run_length = 1;
                reader->state = 0;
            } else if (utf8proc_grapheme_break_stateful(
                reader->previous, codepoint, &reader->state
            )) reader->run_length++;
            reader->previous = codepoint;
        } else {
            reader->seen_text = true;
            if (reader->pending) {
                reader->pending = false;
                *run_length = reader->run_length;
                return true;
            }
        }
    }
    return false;
}

bool patch_source_fuzzy_presentation_init(PatchSourceFuzzyPresentation *result,
    const PatchDocument *original, const PatchSourceFuzzyDocument *corrected) {
    PatchSourceFuzzyPresentation value = {0};
    PatchPresentationBacking *backing;
    size_t count = 0, cursor = 0, file_index;

    if (original->file_count != corrected->document.file_count) return false;
    for (file_index = 0; file_index < original->file_count; file_index++) {
        const PatchFile *left = &original->files[file_index];
        const PatchFile *right = &corrected->document.files[file_index];
        size_t hunk_index;
        if (left->hunk_count != right->hunk_count) return false;
        for (hunk_index = 0; hunk_index < left->hunk_count; hunk_index++) {
            size_t line_index;
            if (left->hunks[hunk_index].line_count !=
                right->hunks[hunk_index].line_count) return false;
            for (line_index = 0; line_index < left->hunks[hunk_index].line_count;
                line_index++) if (left->hunks[hunk_index].lines[line_index].operation == '-') {
                if (count == SIZE_MAX) return false;
                count++;
            }
        }
    }
    if (count > (SIZE_MAX - sizeof(*backing)) / (2 * sizeof(PatchSlice))) return false;
    backing = malloc(sizeof(*backing) + count * 2 * sizeof(PatchSlice));
    if (backing == NULL) return false;
    backing->line_count = count;
    for (file_index = 0; file_index < original->file_count; file_index++) {
        size_t hunk_index;
        for (hunk_index = 0; hunk_index < original->files[file_index].hunk_count;
            hunk_index++) {
            const PatchHunk *left = &original->files[file_index].hunks[hunk_index];
            const PatchHunk *right = &corrected->document.files[file_index].hunks[hunk_index];
            size_t line_index;
            for (line_index = 0; line_index < left->line_count; line_index++) {
                if (left->lines[line_index].operation != '-') continue;
                backing->lines[cursor] = left->lines[line_index].content;
                backing->lines[count + cursor] = right->lines[line_index].content;
                cursor++;
            }
        }
    }
    if (!patch_source_fuzzy_lines_build(&value.comparable, backing->lines, count * 2)) {
        free(backing);
        return false;
    }
    value.backing = (uint8_t *)backing;
    *result = value;
    return true;
}

PatchSourceFuzzyPresentationResult patch_source_fuzzy_presentation_check(
    const PatchSourceFuzzyPresentation *presentation, size_t offset, size_t count) {
    const PatchPresentationBacking *backing =
        (const PatchPresentationBacking *)presentation->backing;
    PatchPresentationRunReader left = {0}, right = {0};
    size_t index;
    if (offset > backing->line_count || count > backing->line_count - offset) {
        return PATCH_FUZZY_PRESENTATION_FAILED;
    }
    for (index = 0; index < count; index++) {
        PatchSlice a = presentation->comparable.lines[offset + index].comparable;
        PatchSlice b = presentation->comparable.lines[backing->line_count + offset + index].comparable;
        if (a.length != b.length || (a.length != 0 && memcmp(a.bytes, b.bytes, a.length) != 0)) {
            return PATCH_FUZZY_PRESENTATION_DIFFERENT;
        }
    }
    left.segments = backing->lines + offset;
    left.segment_count = count;
    right.segments = backing->lines + backing->line_count + offset;
    right.segment_count = count;
    for (;;) {
        size_t a, b;
        bool has_a = patch_presentation_next_run(&left, &a);
        bool has_b = patch_presentation_next_run(&right, &b);
        if (has_a != has_b || (has_a && a != b)) {
            return PATCH_FUZZY_PRESENTATION_WHITESPACE_CHANGED;
        }
        if (!has_a) return PATCH_FUZZY_PRESENTATION_EQUIVALENT;
    }
}

void patch_source_fuzzy_presentation_release(PatchSourceFuzzyPresentation *presentation) {
    free(presentation->backing);
    patch_source_fuzzy_lines_release(&presentation->comparable);
    memset(presentation, 0, sizeof(*presentation));
}
