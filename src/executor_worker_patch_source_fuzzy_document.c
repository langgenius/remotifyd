#include "executor_worker_patch_source_fuzzy_document.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static bool patch_source_fuzzy_document_section(
    size_t *total,
    size_t count,
    size_t item_size,
    size_t *offset
) {
    size_t alignment = _Alignof(max_align_t);
    size_t remainder = *total % alignment;
    size_t padding = remainder == 0 ? 0 : alignment - remainder;
    size_t bytes;

    if (count > SIZE_MAX / item_size || padding > SIZE_MAX - *total) return false;
    bytes = count * item_size;
    if (bytes > SIZE_MAX - *total - padding) return false;
    *offset = *total + padding;
    *total = *offset + bytes;
    return true;
}

bool patch_source_fuzzy_document_init(
    PatchSourceFuzzyDocument *result,
    const PatchDocument *original
) {
    PatchSourceFuzzyDocument corrected = {0};
    size_t hunk_count = 0;
    size_t line_count = 0;
    size_t files_offset, hunks_offset, lines_offset, total = 0;
    size_t file_index;
    size_t hunk_cursor = 0;
    size_t line_cursor = 0;
    uint8_t *allocation;

    if (original->file_count == 0) return false;
    for (file_index = 0; file_index < original->file_count; file_index++) {
        const PatchFile *file = &original->files[file_index];
        size_t hunk_index;
        if (file->hunk_count > SIZE_MAX - hunk_count) return false;
        hunk_count += file->hunk_count;
        for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
            if (file->hunks[hunk_index].line_count > SIZE_MAX - line_count) {
                return false;
            }
            line_count += file->hunks[hunk_index].line_count;
        }
    }
    if (!patch_source_fuzzy_document_section(
        &total, original->file_count, sizeof(PatchFile), &files_offset
    ) || !patch_source_fuzzy_document_section(
        &total, hunk_count, sizeof(PatchHunk), &hunks_offset
    ) || !patch_source_fuzzy_document_section(
        &total, line_count, sizeof(PatchLine), &lines_offset
    )) return false;
    allocation = malloc(total);
    if (allocation == NULL) return false;
    corrected.document.files = (PatchFile *)(allocation + files_offset);
    corrected.document.file_count = original->file_count;

    for (file_index = 0; file_index < original->file_count; file_index++) {
        const PatchFile *source_file = &original->files[file_index];
        PatchFile *file = &corrected.document.files[file_index];
        size_t hunk_index;

        *file = *source_file;
        file->hunks = (PatchHunk *)(allocation + hunks_offset) + hunk_cursor;
        for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
            const PatchHunk *source_hunk = &source_file->hunks[hunk_index];
            PatchHunk *hunk = &file->hunks[hunk_index];
            *hunk = *source_hunk;
            hunk->lines = (PatchLine *)(allocation + lines_offset) + line_cursor;
            memcpy(
                hunk->lines,
                source_hunk->lines,
                hunk->line_count * sizeof(*hunk->lines)
            );
            line_cursor += hunk->line_count;
        }
        hunk_cursor += file->hunk_count;
    }
    *result = corrected;
    return true;
}

bool patch_source_fuzzy_document_apply_file(
    PatchSourceFuzzyDocument *document,
    size_t file_index,
    const PatchSourceFuzzyMatchPlan *plan
) {
    PatchFile *file;
    size_t old_before = 0;
    size_t new_before = 0;
    size_t hunk_index;

    if (file_index >= document->document.file_count) return false;
    file = &document->document.files[file_index];

    /* Preflight the whole file so an invalid plan cannot leave a partially
     * corrected semantic tree for later formatting. */
    for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
        const PatchHunk *hunk = &file->hunks[hunk_index];
        const PatchSourceFuzzyHunkMatch *match = &plan->hunks[hunk_index];
        size_t source_index = match->source_start;
        size_t base;
        size_t delta;
        size_t line_index;

        if (match->source_start == SIZE_MAX ||
            match->source_start > plan->source_line_count ||
            hunk->old_length > plan->source_line_count - match->source_start) {
            return false;
        }
        base = match->source_start + 1;
        if (new_before >= old_before) {
            delta = new_before - old_before;
            if (delta > SIZE_MAX - base) return false;
        } else {
            delta = old_before - new_before;
            if (delta > base) return false;
        }
        for (line_index = 0; line_index < hunk->line_count; line_index++) {
            uint8_t operation = hunk->lines[line_index].operation;
            if (operation == ' ' || operation == '-') {
                if (source_index == plan->source_line_count) return false;
                source_index++;
            } else if (operation != '+') return false;
        }
        if (source_index - match->source_start != hunk->old_length ||
            hunk->old_length > SIZE_MAX - old_before ||
            hunk->new_length > SIZE_MAX - new_before) return false;
        old_before += hunk->old_length;
        new_before += hunk->new_length;
    }

    old_before = 0;
    new_before = 0;
    for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
        PatchHunk *hunk = &file->hunks[hunk_index];
        const PatchSourceFuzzyHunkMatch *match = &plan->hunks[hunk_index];
        size_t source_index = match->source_start;
        size_t base = match->source_start + 1;
        size_t line_index;

        hunk->old_start = base;
        hunk->new_start = new_before >= old_before
            ? base + (new_before - old_before)
            : base - (old_before - new_before);
        for (line_index = 0; line_index < hunk->line_count; line_index++) {
            PatchLine *line = &hunk->lines[line_index];
            if (line->operation == ' ' || line->operation == '-') {
                line->content = plan->source_lines[source_index++];
            }
        }
        old_before += hunk->old_length;
        new_before += hunk->new_length;
    }
    return true;
}

void patch_source_fuzzy_document_release(PatchSourceFuzzyDocument *document) {
    free(document->document.files);
    memset(document, 0, sizeof(*document));
}
