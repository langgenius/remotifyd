#include "executor_worker_patch_preview.h"

#include "executor_worker_patch_path.h"
#include "executor_worker_patch_preview_policy.h"
#include "executor_worker_patch_source.h"

#include <stdlib.h>
#include <string.h>

static bool patch_slice_equals(PatchSlice left, PatchSlice right) {
    return left.length == right.length &&
        (left.length == 0 || memcmp(left.bytes, right.bytes, left.length) == 0);
}

static bool patch_path_is_null(PatchSlice path) {
    static const uint8_t null_path[] = "/dev/null";

    return path.length == sizeof(null_path) - 1 &&
        memcmp(path.bytes, null_path, sizeof(null_path) - 1) == 0;
}

static bool patch_file_operation(
    const PatchFile *file,
    PatchFileOperation *operation
) {
    bool old_null = patch_path_is_null(file->old_path);
    bool new_null = patch_path_is_null(file->new_path);

    if (old_null && !new_null) *operation = PATCH_FILE_CREATE;
    else if (!old_null && new_null) *operation = PATCH_FILE_REMOVE;
    else if (!old_null && patch_slice_equals(file->old_path, file->new_path)) {
        *operation = PATCH_FILE_MODIFY;
    } else if (!old_null && !new_null) *operation = PATCH_FILE_RENAME;
    else return false;
    return true;
}

static bool patch_target_available(uv_loop_t *loop, const PatchPreviewFile *file) {
    uv_fs_t request;
    int result;

    if (file->target_path == NULL ||
        (file->source_path != NULL &&
            strcmp(file->source_path, file->target_path) == 0)) return true;
    result = uv_fs_lstat(loop, &request, file->target_path, NULL);
    uv_fs_req_cleanup(&request);
    return result == UV_ENOENT;
}

static bool patch_target_unique(
    const PatchPreview *preview,
    size_t current
) {
    const char *target = preview->files[current].target_path;
    size_t index;

    if (target == NULL) return true;
    for (index = 0; index < current; index++) {
        const char *previous = preview->files[index].target_path;

        if (previous != NULL && strcmp(previous, target) == 0) return false;
    }
    return true;
}

static bool patch_source_snapshot(
    uv_loop_t *loop,
    PatchPreviewFile *file
) {
    PatchSourceText source = {0};

    if (!patch_source_read_resolved(loop, file->source_path, &source)) return false;
    file->source = source.bytes;
    file->source_length = source.length;
    if (file->operation == PATCH_FILE_MODIFY) file->mode = source.mode;
    return true;
}

bool patch_preview_validate(uv_loop_t *loop, const PatchPreview *preview) {
    size_t index;

    for (index = 0; index < preview->file_count; index++) {
        const PatchPreviewFile *file = &preview->files[index];
        PatchPreviewFile current = {
            .operation = file->operation,
            .source_path = file->source_path
        };
        bool same;

        if (file->operation != PATCH_FILE_CREATE) {
            if (!patch_path_validate(loop, preview->root, file->source_path) ||
                !patch_source_snapshot(loop, &current)) return false;
            same = current.source_length == file->source_length &&
                (current.source_length == 0 || memcmp(
                    current.source, file->source, current.source_length
                ) == 0);
            free(current.source);
            if (!same) return false;
        }
        if (file->target_path != NULL &&
            (file->source_path == NULL ||
                strcmp(file->source_path, file->target_path) != 0) &&
            !patch_path_validate(loop, preview->root, file->target_path)) return false;
        if (!patch_target_available(loop, file)) return false;
    }
    return true;
}

void patch_preview_release(PatchPreview *preview) {
    size_t index;

    for (index = 0; index < preview->file_count; index++) {
        PatchPreviewFile *file = &preview->files[index];

        patch_path_release(&file->source_path);
        patch_path_release(&file->target_path);
        free(file->source);
        patch_applied_text_release(&file->target);
    }
    patch_path_release(&preview->root);
    free(preview->files);
    memset(preview, 0, sizeof(*preview));
}

PatchPreviewResult patch_preview_build(
    PatchPreview *result,
    uv_loop_t *loop,
    const uint8_t *cwd,
    size_t cwd_length,
    const PatchDocument *document
) {
    PatchPreview preview = {0};
    size_t index;

    if (document->file_count == 0 ||
        document->file_count > SIZE_MAX / sizeof(*preview.files) ||
        !patch_root_resolve(loop, cwd, cwd_length, &preview.root)) {
        return PATCH_PREVIEW_FAILED;
    }
    preview.files = calloc(document->file_count, sizeof(*preview.files));
    if (preview.files == NULL) {
        patch_preview_release(&preview);
        return PATCH_PREVIEW_FAILED;
    }
    preview.file_count = document->file_count;

    for (index = 0; index < preview.file_count; index++) {
        const PatchFile *parsed = &document->files[index];
        PatchPreviewFile *file = &preview.files[index];

        if (!patch_file_operation(parsed, &file->operation) ||
            (file->operation != PATCH_FILE_CREATE &&
                !patch_path_resolve(
                    loop, preview.root, parsed->old_path, &file->source_path
                )) ||
            (file->operation != PATCH_FILE_REMOVE &&
                !patch_path_resolve(
                    loop, preview.root, parsed->new_path, &file->target_path
                )) ||
            !patch_target_unique(&preview, index) ||
            !patch_target_available(loop, file)) {
            patch_preview_release(&preview);
            return PATCH_PREVIEW_FAILED;
        }
    }
    for (index = 0; index < preview.file_count; index++) {
        PatchPreviewFile *file = &preview.files[index];

        if (file->operation != PATCH_FILE_CREATE &&
            !patch_source_snapshot(loop, file)) {
            patch_preview_release(&preview);
            return PATCH_PREVIEW_FAILED;
        }
    }
    for (index = 0; index < preview.file_count; index++) {
        if (!patch_preview_policy_allows_file(
            &document->files[index], preview.files[index].source_length
        )) {
            patch_preview_release(&preview);
            return PATCH_PREVIEW_FAILED;
        }
    }
    for (index = 0; index < preview.file_count; index++) {
        const PatchFile *parsed = &document->files[index];
        PatchPreviewFile *file = &preview.files[index];
        PatchExactResult exact_result;

        exact_result = patch_file_apply_exact(
            &file->target, file->source, file->source_length, parsed
        );
        if (exact_result != PATCH_EXACT_SUCCEEDED) {
            patch_preview_release(&preview);
            return exact_result == PATCH_EXACT_MISMATCH
                ? PATCH_PREVIEW_MISMATCH : PATCH_PREVIEW_FAILED;
        }
        if (file->operation == PATCH_FILE_CREATE ||
            file->operation == PATCH_FILE_RENAME) file->mode = 0666;
        if (file->operation == PATCH_FILE_REMOVE) {
            patch_applied_text_release(&file->target);
        }
    }
    *result = preview;
    return PATCH_PREVIEW_SUCCEEDED;
}
