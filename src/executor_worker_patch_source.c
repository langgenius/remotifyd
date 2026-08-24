#include "executor_worker_patch_source.h"

#include "executor_worker_patch_path.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define PATCH_SOURCE_MAX_BYTES (1024u * 1024u)

bool patch_source_read_resolved(
    uv_loop_t *loop,
    const char *path,
    PatchSourceText *result
) {
    uv_fs_t request;
    uv_file descriptor;
    PatchSourceText source = {0};
    size_t received = 0;
    int read_result;
    int status = uv_fs_stat(loop, &request, path, NULL);

    if (status != 0 || (request.statbuf.st_mode & S_IFMT) != S_IFREG ||
        request.statbuf.st_size > PATCH_SOURCE_MAX_BYTES) {
        uv_fs_req_cleanup(&request);
        return false;
    }
    source.length = (size_t)request.statbuf.st_size;
    source.mode = (int)(request.statbuf.st_mode & 0777);
    uv_fs_req_cleanup(&request);

    if (source.length != 0) {
        source.bytes = malloc(source.length);
        if (source.bytes == NULL) return false;
    }
    status = uv_fs_open(loop, &request, path, O_RDONLY, 0, NULL);
    descriptor = status;
    uv_fs_req_cleanup(&request);
    if (status < 0) {
        free(source.bytes);
        return false;
    }

    read_result = 0;
    while (received < source.length) {
        uv_buf_t buffer = uv_buf_init(
            (char *)source.bytes + received,
            (unsigned int)(source.length - received)
        );

        read_result = uv_fs_read(
            loop, &request, descriptor, &buffer, 1, (int64_t)received, NULL
        );
        uv_fs_req_cleanup(&request);
        if (read_result <= 0) break;
        received += (size_t)read_result;
    }
    if (received == source.length) {
        char extra;
        uv_buf_t buffer = uv_buf_init(&extra, 1);

        read_result = uv_fs_read(
            loop, &request, descriptor, &buffer, 1, (int64_t)received, NULL
        );
        uv_fs_req_cleanup(&request);
    }
    status = uv_fs_close(loop, &request, descriptor, NULL);
    uv_fs_req_cleanup(&request);
    if (status != 0 || received != source.length || read_result != 0) {
        free(source.bytes);
        return false;
    }
    *result = source;
    return true;
}

void patch_source_text_release(PatchSourceText *result) {
    free(result->bytes);
    memset(result, 0, sizeof(*result));
}

static bool patch_source_path_equal(PatchSlice left, PatchSlice right) {
    return left.length == right.length &&
        (left.length == 0 || memcmp(left.bytes, right.bytes, left.length) == 0);
}

static bool patch_source_null_path(PatchSlice path) {
    static const uint8_t null_path[] = "/dev/null";

    return path.length == sizeof(null_path) - 1 &&
        memcmp(path.bytes, null_path, sizeof(null_path) - 1) == 0;
}

static bool patch_source_path_seen(
    const PatchDocument *document,
    size_t current
) {
    size_t index;

    for (index = 0; index < current; index++) {
        if (patch_source_path_equal(
            document->files[index].old_path,
            document->files[current].old_path
        )) return true;
    }
    return false;
}

void patch_source_set_release(PatchSourceSet *set) {
    size_t index;

    for (index = 0; index < set->count; index++) {
        patch_source_text_release(&set->entries[index].text);
    }
    free(set->entries);
    memset(set, 0, sizeof(*set));
}

bool patch_source_set_build(
    PatchSourceSet *result,
    uv_loop_t *loop,
    const uint8_t *cwd,
    size_t cwd_length,
    const PatchDocument *document
) {
    PatchSourceSet set = {0};
    char *root = NULL;
    size_t unique_count = 0;
    size_t index;

    for (index = 0; index < document->file_count; index++) {
        if (patch_source_null_path(document->files[index].old_path) ||
            patch_source_path_seen(document, index)) continue;
        unique_count++;
    }
    if (unique_count == 0) {
        *result = set;
        return true;
    }
    if (unique_count > SIZE_MAX / sizeof(*set.entries) ||
        !patch_root_resolve(loop, cwd, cwd_length, &root)) return false;
    set.entries = calloc(unique_count, sizeof(*set.entries));
    if (set.entries == NULL) {
        patch_path_release(&root);
        return false;
    }

    for (index = 0; index < document->file_count; index++) {
        PatchSlice path = document->files[index].old_path;
        char *resolved = NULL;

        if (patch_source_null_path(path) || patch_source_path_seen(document, index)) {
            continue;
        }
        if (!patch_path_resolve(loop, root, path, &resolved) ||
            !patch_source_read_resolved(
                loop, resolved, &set.entries[set.count].text
            )) {
            patch_path_release(&resolved);
            patch_path_release(&root);
            patch_source_set_release(&set);
            return false;
        }
        patch_path_release(&resolved);
        set.entries[set.count].path = path;
        set.count++;
    }
    patch_path_release(&root);
    *result = set;
    return true;
}

const PatchSourceText *patch_source_set_find(
    const PatchSourceSet *set,
    PatchSlice path
) {
    size_t index;

    for (index = 0; index < set->count; index++) {
        if (patch_source_path_equal(set->entries[index].path, path)) {
            return &set->entries[index].text;
        }
    }
    return NULL;
}
