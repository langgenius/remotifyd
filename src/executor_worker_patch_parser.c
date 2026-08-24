#include "executor_worker_patch_parser.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct PatchArena {
    uint8_t *bytes;
    size_t used;
    size_t capacity;
} PatchArena;

static void *patch_arena_allocate(PatchArena *arena, size_t size) {
    const size_t alignment = _Alignof(max_align_t);
    size_t offset = (arena->used + alignment - 1) & ~(alignment - 1);

    if (offset > arena->capacity || size > arena->capacity - offset) return NULL;
    arena->used = offset + size;
    return arena->bytes + offset;
}

static void *patch_arena_zero(PatchArena *arena, size_t count, size_t size) {
    void *result;

    if (size != 0 && count > SIZE_MAX / size) return NULL;
    size *= count;
    result = patch_arena_allocate(arena, size);
    if (result != NULL) memset(result, 0, size);
    return result;
}

static void patch_arena_release(PatchArena *arena) {
    free(arena->bytes);
    memset(arena, 0, sizeof(*arena));
}

typedef struct PatchMeasure {
    size_t file_count;
    size_t hunk_count;
    size_t line_count;
    size_t path_byte_count;
} PatchMeasure;

/*
 * The workspace ends with patch_document_parse. On success, the document
 * owns its one allocation until patch_document_release.
 */
typedef struct PatchWorkspace {
    PatchSlice patch;
    PatchArena arena;
    PatchMeasure measure;
    PatchDocument exact;
    PatchHunk *hunks;
    PatchLine *lines;
    uint8_t *paths;
} PatchWorkspace;

static bool patch_document_allocate(PatchWorkspace *workspace) {
    PatchDocument *document = &workspace->exact;
    const size_t alignment = _Alignof(max_align_t);
    size_t counts[] = {
        workspace->measure.file_count,
        workspace->measure.hunk_count,
        workspace->measure.line_count,
        workspace->measure.path_byte_count
    };
    size_t sizes[] = {
        sizeof(*document->files),
        sizeof(*workspace->hunks),
        sizeof(*workspace->lines),
        sizeof(*workspace->paths)
    };
    size_t capacity = 0;
    size_t index;

    for (index = 0; index < 4; index++) {
        size_t bytes;

        if (counts[index] > SIZE_MAX / sizes[index]) return false;
        bytes = counts[index] * sizes[index];
        if (capacity > SIZE_MAX - (alignment - 1)) return false;
        capacity = (capacity + alignment - 1) & ~(alignment - 1);
        if (bytes > SIZE_MAX - capacity) return false;
        capacity += bytes;
    }
    workspace->arena.bytes = malloc(capacity);
    if (workspace->arena.bytes == NULL) return false;
    workspace->arena.capacity = capacity;

    document->files = patch_arena_zero(
        &workspace->arena,
        workspace->measure.file_count,
        sizeof(*document->files)
    );
    workspace->hunks = patch_arena_zero(
        &workspace->arena,
        workspace->measure.hunk_count,
        sizeof(*workspace->hunks)
    );
    workspace->lines = patch_arena_zero(
        &workspace->arena,
        workspace->measure.line_count,
        sizeof(*workspace->lines)
    );
    workspace->paths = patch_arena_allocate(
        &workspace->arena,
        workspace->measure.path_byte_count
    );
    if (document->files == NULL || workspace->hunks == NULL ||
        workspace->lines == NULL || workspace->paths == NULL) {
        return false;
    }
    document->file_count = workspace->measure.file_count;
    return true;
}

static bool patch_slice_starts(PatchSlice slice, const char *prefix) {
    size_t length = 0;

    while (prefix[length] != '\0') length++;
    return slice.length >= length &&
        (length == 0 || memcmp(slice.bytes, prefix, length) == 0);
}

static bool patch_next_line(PatchSlice input, size_t *cursor, PatchSlice *line) {
    size_t start;

    if (*cursor >= input.length) return false;
    start = *cursor;
    while (*cursor < input.length && input.bytes[*cursor] != '\n') (*cursor)++;
    line->bytes = input.bytes + start;
    line->length = *cursor - start;
    if (*cursor < input.length) (*cursor)++;
    if (line->length != 0 && line->bytes[line->length - 1] == '\r') line->length--;
    return true;
}

static bool patch_parse_number(const uint8_t **cursor, const uint8_t *end, size_t *number) {
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

static bool patch_parse_hunk_header(PatchSlice line, PatchHunk *hunk) {
    const uint8_t *cursor = line.bytes + 4;
    const uint8_t *end = line.bytes + line.length;
    size_t declared_old_length = 1;
    size_t declared_new_length = 1;

    if (!patch_parse_number(&cursor, end, &hunk->old_start)) return false;
    if (cursor < end && *cursor == ',') {
        cursor++;
        if (!patch_parse_number(&cursor, end, &declared_old_length)) return false;
    }
    if (cursor >= end || *cursor++ != ' ' || cursor >= end || *cursor++ != '+') return false;
    if (!patch_parse_number(&cursor, end, &hunk->new_start)) return false;
    if (cursor < end && *cursor == ',') {
        cursor++;
        if (!patch_parse_number(&cursor, end, &declared_new_length)) return false;
    }
    if ((size_t)(end - cursor) < 3 || memcmp(cursor, " @@", 3) != 0) return false;

    hunk->old_length = declared_old_length;
    hunk->new_length = declared_new_length;
    return true;
}

static bool patch_is_space(uint8_t byte) {
    return byte == ' ' || byte == '\t' || byte == '\v' ||
        byte == '\f' || byte == '\r' || byte == '\n';
}

static bool patch_parse_file_header(
    PatchSlice line,
    uint8_t *output,
    PatchSlice *path,
    PatchSlice *header
) {
    size_t start = 3;
    size_t end = line.length;
    size_t tab;
    size_t source_end;
    size_t written = 0;

    while (start < end && patch_is_space(line.bytes[start])) start++;
    while (end > start && patch_is_space(line.bytes[end - 1])) end--;
    tab = start;
    while (tab < end && line.bytes[tab] != '\t') tab++;
    source_end = tab;
    if (source_end >= start + 2 && line.bytes[start] == '"' &&
        line.bytes[source_end - 1] == '"') {
        start++;
        source_end--;
    }
    while (start < source_end) {
        uint8_t byte = line.bytes[start++];

        if (byte == '\0') return false;
        if (byte == '\\' && start < source_end && line.bytes[start] == '\\') start++;
        if (output != NULL) output[written] = byte;
        written++;
    }
    path->bytes = output;
    path->length = written;
    if (tab == end) {
        header->bytes = line.bytes + end;
        header->length = 0;
    } else {
        size_t header_start = tab + 1;

        while (header_start < end && patch_is_space(line.bytes[header_start])) header_start++;
        while (end > header_start && patch_is_space(line.bytes[end - 1])) end--;
        header->bytes = line.bytes + header_start;
        header->length = end - header_start;
    }
    return true;
}

static bool patch_is_hunk_header(PatchSlice line) {
    PatchHunk ignored = {0};

    return patch_slice_starts(line, "@@ -") &&
        patch_parse_hunk_header(line, &ignored);
}

/*
 * A hunk deletion can itself begin with "--- ". Treat it as the next file
 * only when the complete unified-diff boundary is present.
 */
static bool patch_is_file_header(
    PatchSlice input,
    size_t cursor,
    PatchSlice line
) {
    PatchSlice target;
    PatchSlice hunk;

    return patch_slice_starts(line, "--- ") &&
        patch_next_line(input, &cursor, &target) &&
        patch_slice_starts(target, "+++ ") &&
        patch_next_line(input, &cursor, &hunk) &&
        patch_is_hunk_header(hunk);
}

static bool patch_is_metadata_boundary(PatchSlice line) {
    return patch_slice_starts(line, "diff --git ") ||
        patch_slice_starts(line, "Index: ");
}

/*
 * Echo repairs a raw empty hunk line only when it is content. Its split()
 * sentinel and an empty line before the next hunk/file are boundaries; the C
 * scanner does not emit that sentinel, so every empty line it emits is real.
 */
static bool patch_empty_line_is_context(PatchSlice input, size_t cursor) {
    PatchSlice next;

    /* Every empty line emitted by the C scanner is present in the input. */
    if (!patch_next_line(input, &cursor, &next)) return true;
    return !patch_is_hunk_header(next) &&
        !patch_is_file_header(input, cursor, next);
}

/*
 * The first pass allocates nothing. It rejects an incomplete diff structure
 * and measures the exact number of parser records required by the request.
 */
static bool patch_measure(PatchWorkspace *workspace) {
    size_t cursor = 0;
    PatchSlice line;
    bool inside_file = false;
    bool inside_hunk = false;
    bool file_has_hunk = false;
    bool hunk_changed = false;

    while (patch_next_line(workspace->patch, &cursor, &line)) {
        if (patch_is_file_header(workspace->patch, cursor, line)) {
            PatchSlice target;
            PatchSlice old_path;
            PatchSlice new_path;
            PatchSlice ignored_header;

            if (inside_hunk && !hunk_changed) return false;
            if (inside_file && !file_has_hunk) return false;
            if (!patch_next_line(workspace->patch, &cursor, &target)) return false;
            if (!patch_parse_file_header(
                line, NULL, &old_path, &ignored_header
            ) || !patch_parse_file_header(
                target, NULL, &new_path, &ignored_header
            ) || old_path.length > SIZE_MAX - new_path.length ||
                workspace->measure.path_byte_count >
                    SIZE_MAX - old_path.length - new_path.length) return false;
            workspace->measure.path_byte_count += old_path.length + new_path.length;
            workspace->measure.file_count++;
            inside_file = true;
            inside_hunk = false;
            file_has_hunk = false;
            continue;
        }

        if (patch_is_hunk_header(line)) {
            if (!inside_file) return false;
            if (inside_hunk && !hunk_changed) return false;
            workspace->measure.hunk_count++;
            inside_hunk = true;
            file_has_hunk = true;
            hunk_changed = false;
            continue;
        }

        if (inside_hunk && line.length == 0) {
            if (!patch_empty_line_is_context(workspace->patch, cursor)) {
                if (!hunk_changed) return false;
                inside_hunk = false;
                continue;
            }
            workspace->measure.line_count++;
            continue;
        }

        if (inside_hunk &&
            (line.bytes[0] == ' ' || line.bytes[0] == '+' || line.bytes[0] == '-')) {
            workspace->measure.line_count++;
            if (line.bytes[0] == '+' || line.bytes[0] == '-') hunk_changed = true;
            continue;
        }

        if (inside_hunk && patch_slice_starts(line, "\\ No newline at end of file")) continue;
        if (patch_is_metadata_boundary(line)) {
            if (inside_hunk && !hunk_changed) return false;
            inside_hunk = false;
            inside_file = false;
            continue;
        }
        if (inside_hunk) return false;
        if (patch_slice_starts(line, "--- ") ||
            patch_slice_starts(line, "+++ ") ||
            patch_slice_starts(line, "@@")) return false;
    }

    if (inside_hunk && !hunk_changed) return false;
    return inside_file && file_has_hunk &&
        workspace->measure.file_count != 0 &&
        workspace->measure.hunk_count != 0 &&
        workspace->measure.line_count != 0;
}

static bool patch_finish_hunk(
    PatchHunk *hunk, size_t declared_old_length, size_t declared_new_length
) {
    if (hunk == NULL) return true;
    if (hunk->old_length != declared_old_length ||
        hunk->new_length != declared_new_length) return false;
    if ((hunk->old_length == 0 && hunk->old_start == SIZE_MAX) ||
        (hunk->new_length == 0 && hunk->new_start == SIZE_MAX)) return false;
    if (hunk->old_length == 0) hunk->old_start++;
    if (hunk->new_length == 0) hunk->new_start++;
    return true;
}

static bool patch_parse_document(PatchWorkspace *workspace) {
    PatchDocument *document = &workspace->exact;
    size_t cursor = 0;
    size_t file_index = 0;
    size_t hunk_index = 0;
    size_t line_index = 0;
    size_t path_index = 0;
    size_t declared_old_length = 0;
    size_t declared_new_length = 0;
    PatchFile *file = NULL;
    PatchHunk *hunk = NULL;
    PatchSlice line;

    while (patch_next_line(workspace->patch, &cursor, &line)) {
        bool crlf = line.bytes + line.length <
            workspace->patch.bytes + workspace->patch.length &&
            line.bytes[line.length] == '\r';

        if (patch_is_file_header(workspace->patch, cursor, line)) {
            PatchSlice target;

            if (file_index >= document->file_count ||
                !patch_next_line(workspace->patch, &cursor, &target)) return false;
            if (!patch_finish_hunk(hunk, declared_old_length, declared_new_length)) return false;
            file = &document->files[file_index++];
            file->hunks = &workspace->hunks[hunk_index];
            if (!patch_parse_file_header(
                line, workspace->paths + path_index,
                &file->old_path, &file->old_header
            )) return false;
            path_index += file->old_path.length;
            if (!patch_parse_file_header(
                target, workspace->paths + path_index,
                &file->new_path, &file->new_header
            )) return false;
            path_index += file->new_path.length;
            hunk = NULL;
            continue;
        }

        if (patch_is_hunk_header(line)) {
            if (file == NULL || hunk_index >= workspace->measure.hunk_count) return false;
            if (!patch_finish_hunk(hunk, declared_old_length, declared_new_length)) return false;
            hunk = &workspace->hunks[hunk_index++];
            hunk->lines = &workspace->lines[line_index];
            if (!patch_parse_hunk_header(line, hunk)) return false;
            declared_old_length = hunk->old_length;
            declared_new_length = hunk->new_length;
            hunk->old_length = 0;
            hunk->new_length = 0;
            file->hunk_count++;
            continue;
        }

        if (hunk != NULL && line.length == 0 &&
            !patch_empty_line_is_context(workspace->patch, cursor)) {
            if (!patch_finish_hunk(hunk, declared_old_length, declared_new_length)) return false;
            hunk = NULL;
            continue;
        }

        if (hunk != NULL && (line.length == 0 || line.bytes[0] == ' ' ||
            line.bytes[0] == '+' || line.bytes[0] == '-')) {
            PatchLine *parsed;

            if (line_index >= workspace->measure.line_count) return false;
            parsed = &workspace->lines[line_index++];
            parsed->operation = line.length == 0 ? ' ' : line.bytes[0];
            parsed->content.bytes = line.bytes + (line.length == 0 ? 0 : 1);
            parsed->content.length = line.length == 0 ? 0 : line.length - 1;
            parsed->crlf = crlf;
            hunk->line_count++;
            if (parsed->operation != '+') hunk->old_length++;
            if (parsed->operation != '-') hunk->new_length++;
            continue;
        }

        if (hunk != NULL && patch_slice_starts(line, "\\ No newline at end of file")) {
            if (hunk->line_count == 0) return false;
            hunk->lines[hunk->line_count - 1].no_final_newline = true;
            continue;
        }

        if (patch_is_metadata_boundary(line)) {
            if (!patch_finish_hunk(hunk, declared_old_length, declared_new_length)) return false;
            hunk = NULL;
            file = NULL;
            continue;
        }
        if (hunk != NULL) return false;
        if (patch_slice_starts(line, "--- ") ||
            patch_slice_starts(line, "+++ ") ||
            patch_slice_starts(line, "@@")) return false;
    }

    if (!patch_finish_hunk(hunk, declared_old_length, declared_new_length)) return false;
    return file_index == document->file_count &&
        hunk_index == workspace->measure.hunk_count &&
        line_index == workspace->measure.line_count &&
        path_index == workspace->measure.path_byte_count;
}

bool patch_document_parse(
    PatchDocument *document,
    const uint8_t *patch,
    size_t patch_length
) {
    PatchWorkspace workspace = {
        .patch = {.bytes = patch, .length = patch_length}
    };

    if (!patch_measure(&workspace) ||
        !patch_document_allocate(&workspace) ||
        !patch_parse_document(&workspace)) {
        patch_arena_release(&workspace.arena);
        return false;
    }
    *document = workspace.exact;
    return true;
}

void patch_document_release(PatchDocument *document) {
    free(document->files);
    memset(document, 0, sizeof(*document));
}
