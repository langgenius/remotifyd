#include "executor_worker_patch_format.h"

#include <stdlib.h>
#include <string.h>

typedef struct PatchFormatPass {
    uint8_t *output;
    size_t length;
    size_t capacity;
} PatchFormatPass;

static bool patch_format_append(
    PatchFormatPass *pass,
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

static bool patch_format_byte(PatchFormatPass *pass, uint8_t byte) {
    return patch_format_append(pass, &byte, 1);
}

static bool patch_format_number(PatchFormatPass *pass, size_t number) {
    uint8_t bytes[3 * sizeof(size_t)];
    size_t cursor = sizeof(bytes);

    do {
        bytes[--cursor] = (uint8_t)('0' + number % 10);
        number /= 10;
    } while (number != 0);
    return patch_format_append(pass, bytes + cursor, sizeof(bytes) - cursor);
}

static bool patch_format_header(
    PatchFormatPass *pass,
    const char *prefix,
    PatchSlice path,
    PatchSlice header
) {
    size_t prefix_length = strlen(prefix);

    if (!patch_format_append(pass, (const uint8_t *)prefix, prefix_length) ||
        !patch_format_append(pass, path.bytes, path.length)) return false;
    if (header.bytes != NULL && (!patch_format_byte(pass, '\t') ||
        !patch_format_append(pass, header.bytes, header.length))) return false;
    return patch_format_byte(pass, '\n');
}

static bool patch_format_hunk(PatchFormatPass *pass, const PatchHunk *hunk) {
    static const uint8_t prefix[] = "@@ -";
    static const uint8_t separator[] = " +";
    static const uint8_t close[] = " @@\n";
    static const uint8_t no_newline[] = "\\ No newline at end of file\n";
    size_t old_start = hunk->old_start;
    size_t new_start = hunk->new_start;
    size_t index;

    if ((hunk->old_length == 0 && old_start == 0) ||
        (hunk->new_length == 0 && new_start == 0)) return false;
    if (hunk->old_length == 0) old_start--;
    if (hunk->new_length == 0) new_start--;
    if (!patch_format_append(pass, prefix, sizeof(prefix) - 1) ||
        !patch_format_number(pass, old_start) || !patch_format_byte(pass, ',') ||
        !patch_format_number(pass, hunk->old_length) ||
        !patch_format_append(pass, separator, sizeof(separator) - 1) ||
        !patch_format_number(pass, new_start) || !patch_format_byte(pass, ',') ||
        !patch_format_number(pass, hunk->new_length) ||
        !patch_format_append(pass, close, sizeof(close) - 1)) return false;

    for (index = 0; index < hunk->line_count; index++) {
        const PatchLine *line = &hunk->lines[index];

        if (!patch_format_byte(pass, line->operation) ||
            !patch_format_append(pass, line->content.bytes, line->content.length) ||
            !patch_format_byte(pass, '\n')) return false;
        if (line->no_final_newline && !patch_format_append(
            pass, no_newline, sizeof(no_newline) - 1
        )) return false;
    }
    return true;
}

static bool patch_format_file(PatchFormatPass *pass, const PatchFile *file) {
    size_t index;

    if (!patch_format_header(pass, "--- ", file->old_path, file->old_header) ||
        !patch_format_header(pass, "+++ ", file->new_path, file->new_header)) return false;
    for (index = 0; index < file->hunk_count; index++) {
        if (!patch_format_hunk(pass, &file->hunks[index])) return false;
    }
    return true;
}

static bool patch_format_document(
    PatchFormatPass *pass,
    const PatchDocument *document
) {
    size_t index;

    for (index = 0; index < document->file_count; index++) {
        if (!patch_format_file(pass, &document->files[index])) return false;
    }
    return document->file_count != 0;
}

bool patch_file_format(PatchFormattedText *result, const PatchFile *file) {
    PatchFormatPass measure = {0};
    PatchFormatPass write = {0};
    PatchFormattedText formatted = {0};
    uint8_t *output;

    if (!patch_format_file(&measure, file) || measure.length == 0) return false;
    output = malloc(measure.length);
    if (output == NULL) return false;
    write.output = output;
    write.capacity = measure.length;
    if (!patch_format_file(&write, file) || write.length != measure.length) {
        free(output);
        return false;
    }
    formatted.bytes = output;
    formatted.length = write.length;
    *result = formatted;
    return true;
}

bool patch_document_format(PatchFormattedText *result, const PatchDocument *document) {
    PatchFormatPass measure = {0};
    PatchFormatPass write = {0};
    PatchFormattedText formatted = {0};
    uint8_t *output;

    if (!patch_format_document(&measure, document) || measure.length == 0) return false;
    output = malloc(measure.length);
    if (output == NULL) return false;
    write.output = output;
    write.capacity = measure.length;
    if (!patch_format_document(&write, document) || write.length != measure.length) {
        free(output);
        return false;
    }
    formatted.bytes = output;
    formatted.length = write.length;
    *result = formatted;
    return true;
}

void patch_formatted_text_release(PatchFormattedText *result) {
    free(result->bytes);
    memset(result, 0, sizeof(*result));
}
