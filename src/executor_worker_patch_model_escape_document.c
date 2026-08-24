#include "executor_worker_patch_model_escape_document.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static bool patch_model_section(
    size_t *total, size_t count, size_t item_size, size_t *offset
) {
    size_t alignment = _Alignof(max_align_t);
    size_t padding = *total % alignment == 0 ? 0 : alignment - *total % alignment;
    size_t bytes;

    if (count > SIZE_MAX / item_size || padding > SIZE_MAX - *total) return false;
    bytes = count * item_size;
    if (bytes > SIZE_MAX - *total - padding) return false;
    *offset = *total + padding;
    *total = *offset + bytes;
    return true;
}

static size_t patch_model_extra_lines(const PatchLine *line) {
    size_t extra = 0;
    size_t cursor = 0;

    if (line->operation != ' ' && line->operation != '-') return 0;
    while (cursor + 1 < line->content.length) {
        if (line->content.bytes[cursor] == '\\' &&
            line->content.bytes[cursor + 1] == 'n') {
            extra++;
            cursor += 2;
        } else cursor++;
    }
    return extra;
}

static bool patch_model_line_counts(PatchHunk *hunk, uint8_t operation, size_t count) {
    size_t old_add = operation == '+' ? 0 : count;
    size_t new_add = operation == '-' ? 0 : count;

    if (operation != ' ' && operation != '+' && operation != '-') return false;
    return old_add <= SIZE_MAX - hunk->old_length &&
        new_add <= SIZE_MAX - hunk->new_length;
}

bool patch_model_escape_document_init(
    PatchModelEscapeDocument *result, const PatchDocument *original
) {
    PatchModelEscapeDocument corrected = {0};
    size_t file_count = original->file_count;
    size_t hunk_count = 0, line_count = 0, content_count = 0;
    size_t files_offset, hunks_offset, lines_offset, content_offset, total = 0;
    size_t file_index, hunk_cursor = 0, line_cursor = 0;
    uint8_t *allocation;

    for (file_index = 0; file_index < file_count; file_index++) {
        const PatchFile *file = &original->files[file_index];
        size_t hunk_index;
        if (file->hunk_count > SIZE_MAX - hunk_count) return false;
        hunk_count += file->hunk_count;
        for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
            const PatchHunk *hunk = &file->hunks[hunk_index];
            size_t line_index;
            if (hunk->line_count > SIZE_MAX - line_count) return false;
            line_count += hunk->line_count;
            for (line_index = 0; line_index < hunk->line_count; line_index++) {
                const PatchLine *line = &hunk->lines[line_index];
                size_t extra = patch_model_extra_lines(line);
                if (extra > SIZE_MAX - line_count) return false;
                line_count += extra;
                if ((line->operation == ' ' || line->operation == '-') &&
                    line->content.length > SIZE_MAX - content_count) return false;
                if (line->operation == ' ' || line->operation == '-') {
                    content_count += line->content.length;
                }
            }
        }
    }
    if (!patch_model_section(&total, file_count, sizeof(PatchFile), &files_offset) ||
        !patch_model_section(&total, hunk_count, sizeof(PatchHunk), &hunks_offset) ||
        !patch_model_section(&total, line_count, sizeof(PatchLine), &lines_offset) ||
        !patch_model_section(&total, content_count, 1, &content_offset)) return false;
    allocation = malloc(total);
    if (allocation == NULL) return false;
    corrected.document.files = (PatchFile *)(allocation + files_offset);
    corrected.document.file_count = file_count;
    corrected.hunks_end = (PatchHunk *)(allocation + hunks_offset) + hunk_count;
    corrected.lines_end = (PatchLine *)(allocation + lines_offset) + line_count;
    corrected.content_cursor = allocation + content_offset;
    corrected.content_end = corrected.content_cursor + content_count;

    for (file_index = 0; file_index < file_count; file_index++) {
        const PatchFile *source_file = &original->files[file_index];
        PatchFile *file = &corrected.document.files[file_index];
        size_t hunk_index;
        *file = *source_file;
        file->hunks = (PatchHunk *)(allocation + hunks_offset) + hunk_cursor;
        for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
            const PatchHunk *source_hunk = &source_file->hunks[hunk_index];
            PatchHunk *hunk = &file->hunks[hunk_index];
            size_t line_index, capacity = source_hunk->line_count;
            for (line_index = 0; line_index < source_hunk->line_count; line_index++) {
                capacity += patch_model_extra_lines(&source_hunk->lines[line_index]);
            }
            *hunk = *source_hunk;
            hunk->lines = (PatchLine *)(allocation + lines_offset) + line_cursor;
            hunk->line_count = hunk->old_length = hunk->new_length = 0;
            line_cursor += capacity;
        }
        hunk_cursor += file->hunk_count;
    }
    *result = corrected;
    return true;
}

bool patch_model_escape_document_append_original(
    PatchModelEscapeDocument *document, PatchHunk *hunk, const PatchLine *line
) {
    /* Init assigns consecutive line slices, so the next hunk begins exactly
     * where this hunk's fixed capacity ends. */
    PatchLine *limit = hunk + 1 == document->hunks_end
        ? document->lines_end : hunk[1].lines;
    if (hunk->line_count == (size_t)(limit - hunk->lines) ||
        !patch_model_line_counts(hunk, line->operation, 1)) return false;
    hunk->lines[hunk->line_count++] = *line;
    if (line->operation != '+') hunk->old_length++;
    if (line->operation != '-') hunk->new_length++;
    return true;
}

bool patch_model_escape_document_append_decoded(
    PatchModelEscapeDocument *document, PatchHunk *hunk,
    const PatchLine *line, const PatchSlice *segments, size_t segment_count
) {
    PatchLine *limit = hunk + 1 == document->hunks_end
        ? document->lines_end : hunk[1].lines;
    uint8_t *content = document->content_cursor;
    size_t index, bytes = 0;
    if (segment_count == 0 || segment_count > (size_t)(limit - hunk->lines) - hunk->line_count ||
        !patch_model_line_counts(hunk, line->operation, segment_count)) return false;
    for (index = 0; index < segment_count; index++) {
        if (segments[index].length > SIZE_MAX - bytes) return false;
        bytes += segments[index].length;
    }
    if (bytes > (size_t)(document->content_end - content)) return false;
    for (index = 0; index < segment_count; index++) {
        PatchLine *output = &hunk->lines[hunk->line_count + index];
        *output = *line;
        if (segments[index].length != 0) memcpy(content, segments[index].bytes, segments[index].length);
        output->content.bytes = content;
        output->content.length = segments[index].length;
        output->no_final_newline = line->no_final_newline && index + 1 == segment_count;
        content += segments[index].length;
    }
    hunk->line_count += segment_count;
    if (line->operation != '+') hunk->old_length += segment_count;
    if (line->operation != '-') hunk->new_length += segment_count;
    document->content_cursor = content;
    return true;
}

void patch_model_escape_document_release(PatchModelEscapeDocument *document) {
    free(document->document.files);
    memset(document, 0, sizeof(*document));
}
