#include "executor_worker_patch_git_path.h"

#include "executor_worker_patch_format.h"
#include "executor_worker_patch_parser.h"

#include <stdlib.h>
#include <string.h>

static bool patch_path_starts(PatchSlice path, uint8_t first, uint8_t second) {
    return path.length >= 2 && path.bytes[0] == first && path.bytes[1] == second;
}

bool patch_git_paths_prepare(
    PatchGitPathText *result,
    const uint8_t *bytes,
    size_t length
) {
    PatchDocument document = {0};
    PatchFormattedText formatted = {0};
    PatchGitPathText prepared = {0};
    bool changed = false;
    size_t index;

    if (!patch_document_parse(&document, bytes, length)) return false;
    for (index = 0; index < document.file_count; index++) {
        PatchFile *file = &document.files[index];

        /* A creation target beginning with b/ is ambiguous between a real
         * directory and a Git prefix, so this boundary must not guess. */
        if (file->old_path.length == sizeof("/dev/null") - 1 &&
            memcmp(file->old_path.bytes, "/dev/null", sizeof("/dev/null") - 1) == 0 &&
            patch_path_starts(file->new_path, 'b', '/')) {
            patch_document_release(&document);
            return false;
        }
        if (file->old_path.length <= 2 ||
            file->old_path.length != file->new_path.length ||
            !patch_path_starts(file->old_path, 'a', '/') ||
            !patch_path_starts(file->new_path, 'b', '/') ||
            memcmp(
                file->old_path.bytes + 2,
                file->new_path.bytes + 2,
                file->old_path.length - 2
            ) != 0) continue;
        file->old_path.bytes += 2;
        file->old_path.length -= 2;
        file->new_path.bytes += 2;
        file->new_path.length -= 2;
        changed = true;
    }
    if (changed && !patch_document_format(&formatted, &document)) {
        patch_document_release(&document);
        return false;
    }
    patch_document_release(&document);
    if (changed) {
        prepared.bytes = formatted.bytes;
        prepared.length = formatted.length;
        prepared.owned = formatted.bytes;
    } else {
        prepared.bytes = bytes;
        prepared.length = length;
    }
    *result = prepared;
    return true;
}

void patch_git_path_text_release(PatchGitPathText *result) {
    free(result->owned);
    memset(result, 0, sizeof(*result));
}
