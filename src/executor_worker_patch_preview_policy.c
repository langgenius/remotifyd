#include "executor_worker_patch_preview_policy.h"

#include <string.h>

static bool patch_policy_null_path(PatchSlice path) {
    static const uint8_t null_path[] = "/dev/null";

    return path.length == sizeof(null_path) - 1 &&
        memcmp(path.bytes, null_path, sizeof(null_path) - 1) == 0;
}

/* Use Go's fixed Unicode White_Space set instead of the process locale;
 * invalid UTF-8 deliberately remains nonblank. */
static size_t patch_policy_space(const uint8_t *bytes, size_t length) {
    uint8_t first = bytes[0];

    if ((first >= 0x09 && first <= 0x0d) || first == 0x20) return 1;
    if (length >= 2 && first == 0xc2 &&
        (bytes[1] == 0x85 || bytes[1] == 0xa0)) return 2;
    if (length < 3) return 0;
    if (first == 0xe1 && bytes[1] == 0x9a && bytes[2] == 0x80) return 3;
    if (first == 0xe2 && bytes[1] == 0x80 &&
        ((bytes[2] >= 0x80 && bytes[2] <= 0x8a) ||
            bytes[2] == 0xa8 || bytes[2] == 0xa9 || bytes[2] == 0xaf)) return 3;
    if (first == 0xe2 && bytes[1] == 0x81 && bytes[2] == 0x9f) return 3;
    if (first == 0xe3 && bytes[1] == 0x80 && bytes[2] == 0x80) return 3;
    return 0;
}

static bool patch_policy_context_nonblank(PatchSlice content) {
    size_t cursor = 0;

    while (cursor < content.length) {
        size_t consumed = patch_policy_space(
            content.bytes + cursor, content.length - cursor
        );

        if (consumed == 0) return true;
        cursor += consumed;
    }
    return false;
}

static bool patch_policy_batch_allowed(const PatchFile *file) {
    size_t hunk_index;

    for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
        const PatchHunk *hunk = &file->hunks[hunk_index];
        size_t added = 0;
        size_t removed = 0;
        size_t line_index;

        for (line_index = 0; line_index < hunk->line_count; line_index++) {
            uint8_t operation = hunk->lines[line_index].operation;

            if (operation == '+') added++;
            else if (operation == '-') removed++;
        }
        if (added > 40 && removed > 40) return false;
    }
    return true;
}

static bool patch_policy_insertions_allowed(const PatchFile *file) {
    size_t hunk_index;

    for (hunk_index = 0; hunk_index < file->hunk_count; hunk_index++) {
        const PatchHunk *hunk = &file->hunks[hunk_index];
        bool addition = false;
        bool deletion = false;
        bool nonblank_context = false;
        size_t line_index;

        for (line_index = 0; line_index < hunk->line_count; line_index++) {
            const PatchLine *line = &hunk->lines[line_index];

            if (line->operation == '+') addition = true;
            else if (line->operation == '-') deletion = true;
            else if (line->operation == ' ' &&
                patch_policy_context_nonblank(line->content)) nonblank_context = true;
        }
        if (addition && !deletion && !nonblank_context) return false;
    }
    return true;
}

bool patch_preview_policy_allows_file(
    const PatchFile *file,
    size_t source_length
) {
    if (patch_policy_null_path(file->old_path) ||
        patch_policy_null_path(file->new_path)) return true;
    return patch_policy_batch_allowed(file) &&
        (source_length == 0 || patch_policy_insertions_allowed(file));
}
