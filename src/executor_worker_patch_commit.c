#include "executor_worker_patch_commit.h"

#include "logger.h"
#include "random.h"

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct PatchCommitFile {
    char *staged_path;
    char *backup_dir;
    char *backup_path;
    int mode;
    bool source_moved;
    bool target_installed;
} PatchCommitFile;

typedef struct PatchCommit {
    PatchCommitFile *files;
    char **created_directories;
    size_t created_count;
    size_t created_capacity;
} PatchCommit;

static bool patch_commit_separator(char byte) {
#ifdef _WIN32
    return byte == '/' || byte == '\\';
#else
    return byte == '/';
#endif
}

static bool patch_created_capacity(
    const PatchPreview *preview,
    size_t *capacity
) {
    size_t root_length = strlen(preview->root);
    size_t total = 0;
    size_t index;

    for (index = 0; index < preview->file_count; index++) {
        const char *target = preview->files[index].target_path;
        size_t cursor;

        if (target == NULL) continue;
        for (cursor = root_length; target[cursor] != 0; cursor++) {
            if (patch_commit_separator(target[cursor])) {
                if (total == SIZE_MAX) return false;
                total++;
            }
        }
    }
    *capacity = total;
    return true;
}

static char *patch_path_prefix(const char *path, size_t length) {
    char *prefix;

    if (length == SIZE_MAX) return NULL;
    prefix = malloc(length + 1);
    if (prefix == NULL) return NULL;
    memcpy(prefix, path, length);
    prefix[length] = 0;
    return prefix;
}

static bool patch_commit_make_parents(
    uv_loop_t *loop,
    const PatchPreview *preview,
    PatchCommit *commit,
    const char *target
) {
    size_t root_length = strlen(preview->root);
    size_t target_length = strlen(target);
    size_t final_separator = target_length;
    size_t cursor;

    while (final_separator > root_length &&
        !patch_commit_separator(target[final_separator - 1])) final_separator--;
    if (final_separator == root_length) return true;
    final_separator--;

    for (cursor = root_length; cursor <= final_separator; cursor++) {
        uv_fs_t request;
        char *directory;
        int result;

        if (!patch_commit_separator(target[cursor]) || cursor == root_length) continue;
        directory = patch_path_prefix(target, cursor);
        if (directory == NULL) return false;
        result = uv_fs_lstat(loop, &request, directory, NULL);
        if (result == 0 && (request.statbuf.st_mode & S_IFMT) != S_IFDIR) {
            result = UV_ENOTDIR;
        }
        uv_fs_req_cleanup(&request);
        if (result == 0) {
            free(directory);
            continue;
        }
        if (result != UV_ENOENT || commit->created_count == commit->created_capacity) {
            free(directory);
            return false;
        }
        result = uv_fs_mkdir(loop, &request, directory, 0755, NULL);
        uv_fs_req_cleanup(&request);
        if (result != 0) {
            free(directory);
            return false;
        }
        commit->created_directories[commit->created_count++] = directory;
    }
    return true;
}

static void patch_commit_cleanup_directories(uv_loop_t *loop, PatchCommit *commit) {
    while (commit->created_count != 0) {
        uv_fs_t request;
        char *directory = commit->created_directories[--commit->created_count];
        int result = uv_fs_rmdir(loop, &request, directory, NULL);

        uv_fs_req_cleanup(&request);
        if (result != 0) logf_error("failed to clean staged patch directory: %s", directory);
        free(directory);
        commit->created_directories[commit->created_count] = NULL;
    }
}

static char *patch_commit_random_path(const char *file_path) {
    static const char prefix[] = ".remotify-patch-";
    static const char hexadecimal[] = "0123456789abcdef";
    uint8_t random[16];
    size_t directory_length = strlen(file_path);
    size_t length;
    size_t index;
    char *path;

    while (directory_length != 0 &&
        !patch_commit_separator(file_path[directory_length - 1])) directory_length--;
    if (directory_length > SIZE_MAX - sizeof(prefix) - sizeof(random) * 2) return NULL;
    length = directory_length + sizeof(prefix) - 1 + sizeof(random) * 2;
    path = malloc(length + 1);
    if (path == NULL || !random_bytes(random, sizeof(random))) {
        free(path);
        return NULL;
    }
    memcpy(path, file_path, directory_length);
    memcpy(path + directory_length, prefix, sizeof(prefix) - 1);
    for (index = 0; index < sizeof(random); index++) {
        path[directory_length + sizeof(prefix) - 1 + index * 2] =
            hexadecimal[random[index] >> 4];
        path[directory_length + sizeof(prefix) + index * 2] =
            hexadecimal[random[index] & 15];
    }
    path[length] = 0;
    return path;
}

static bool patch_commit_stage_file(
    uv_loop_t *loop,
    const PatchPreviewFile *preview,
    PatchCommitFile *file
) {
    uv_fs_t request;
    uv_file descriptor;
    char *path;
    size_t written = 0;
    bool succeed = true;
    int result;

#if SIZE_MAX > INT64_MAX
    if (preview->target.length > INT64_MAX) return false;
#endif
    path = patch_commit_random_path(preview->target_path);
    if (path == NULL) return false;
    result = uv_fs_open(
        loop, &request, path, O_CREAT | O_EXCL | O_WRONLY, preview->mode, NULL
    );
    descriptor = result;
    uv_fs_req_cleanup(&request);
    if (result < 0) {
        free(path);
        return false;
    }
    file->staged_path = path;
    file->mode = preview->mode;

    while (written < preview->target.length) {
        size_t remaining = preview->target.length - written;
        unsigned int chunk = remaining > INT_MAX ? INT_MAX : (unsigned int)remaining;
        uv_buf_t buffer = uv_buf_init((char *)preview->target.bytes + written, chunk);

        result = uv_fs_write(
            loop, &request, descriptor, &buffer, 1, (int64_t)written, NULL
        );
        uv_fs_req_cleanup(&request);
        if (result <= 0) {
            succeed = false;
            break;
        }
        written += (size_t)result;
    }
    if (succeed) {
        if (uv_fs_fsync(loop, &request, descriptor, NULL) != 0) succeed = false;
        uv_fs_req_cleanup(&request);
    }
    if (succeed && (preview->operation == PATCH_FILE_CREATE ||
        preview->operation == PATCH_FILE_RENAME)) {
        result = uv_fs_fstat(loop, &request, descriptor, NULL);
        if (result == 0) file->mode = (int)(request.statbuf.st_mode & 0777);
        else succeed = false;
        uv_fs_req_cleanup(&request);
    }
    result = uv_fs_close(loop, &request, descriptor, NULL);
    uv_fs_req_cleanup(&request);
    return succeed && result == 0;
}

static int patch_commit_remove_owned(uv_loop_t *loop, const char *path) {
    uv_fs_t request;
    int result;

#ifdef _WIN32
    result = uv_fs_chmod(loop, &request, path, 0600, NULL);
    uv_fs_req_cleanup(&request);
    if (result != 0 && result != UV_ENOENT) return result;
#endif
    result = uv_fs_unlink(loop, &request, path, NULL);
    uv_fs_req_cleanup(&request);
    return result;
}

static void patch_commit_cleanup_staged(
    uv_loop_t *loop,
    PatchCommit *commit,
    size_t file_count
) {
    size_t index;

    for (index = 0; index < file_count; index++) {
        PatchCommitFile *file = &commit->files[index];

        if (file->staged_path == NULL) continue;
        int result = patch_commit_remove_owned(loop, file->staged_path);

        if (result != 0 && result != UV_ENOENT) {
            logf_error("failed to clean staged patch file: %s", file->staged_path);
        }
        free(file->staged_path);
        file->staged_path = NULL;
    }
}

static char *patch_commit_backup_path(const char *backup_dir) {
    static const char name[] = "original";
    size_t directory_length = strlen(backup_dir);
    char *path;

    if (directory_length > SIZE_MAX - 1 - sizeof(name)) return NULL;
    path = malloc(directory_length + 1 + sizeof(name));
    if (path == NULL) return NULL;
    memcpy(path, backup_dir, directory_length);
#ifdef _WIN32
    path[directory_length] = '\\';
#else
    path[directory_length] = '/';
#endif
    memcpy(path + directory_length + 1, name, sizeof(name));
    return path;
}

static bool patch_commit_backup_source(
    uv_loop_t *loop,
    const PatchPreviewFile *preview,
    PatchCommitFile *file
) {
    uv_fs_t request;
    char *directory = patch_commit_random_path(preview->source_path);
    char *path;
    int result;

    if (directory == NULL) return false;
    path = patch_commit_backup_path(directory);
    if (path == NULL) {
        free(directory);
        return false;
    }
    result = uv_fs_mkdir(loop, &request, directory, 0700, NULL);
    uv_fs_req_cleanup(&request);
    if (result != 0) {
        free(path);
        free(directory);
        return false;
    }
    file->backup_dir = directory;
    file->backup_path = path;
    result = uv_fs_rename(
        loop, &request, preview->source_path, file->backup_path, NULL
    );
    uv_fs_req_cleanup(&request);
    if (result != 0) return false;
    file->source_moved = true;
    return true;
}

static bool patch_commit_file(
    uv_loop_t *loop,
    const PatchPreviewFile *preview,
    PatchCommitFile *file
) {
    uv_fs_t request;
    int result;

    if (preview->operation != PATCH_FILE_CREATE &&
        !patch_commit_backup_source(loop, preview, file)) return false;
    if (preview->operation == PATCH_FILE_REMOVE) return true;
    result = uv_fs_rename(
        loop, &request, file->staged_path, preview->target_path, NULL
    );
    uv_fs_req_cleanup(&request);
    if (result != 0) return false;
    free(file->staged_path);
    file->staged_path = NULL;
    file->target_installed = true;
    result = uv_fs_chmod(loop, &request, preview->target_path, file->mode, NULL);
    uv_fs_req_cleanup(&request);
    return result == 0;
}

static bool patch_commit_close_backup(uv_loop_t *loop, PatchCommitFile *file) {
    uv_fs_t request;
    int result = uv_fs_rmdir(loop, &request, file->backup_dir, NULL);

    uv_fs_req_cleanup(&request);
    if (result != 0) return false;
    free(file->backup_path);
    free(file->backup_dir);
    file->backup_path = NULL;
    file->backup_dir = NULL;
    return true;
}

static bool patch_commit_rollback(
    uv_loop_t *loop,
    const PatchPreview *preview,
    PatchCommit *commit
) {
    bool succeed = true;
    size_t index = preview->file_count;

    /* Continue the reverse rollback after an error so every touched file gets
     * its best chance to recover; any failed step makes the result uncertain. */
    while (index != 0) {
        const PatchPreviewFile *preview_file = &preview->files[--index];
        PatchCommitFile *file = &commit->files[index];
        uv_fs_t request;
        int result;

        if (file->target_installed) {
            result = patch_commit_remove_owned(loop, preview_file->target_path);
            if (result == 0) file->target_installed = false;
            else {
                logf_error("failed to remove rolled back patch target: %s", preview_file->target_path);
                succeed = false;
            }
        }
        if (file->source_moved) {
            result = uv_fs_rename(
                loop, &request, file->backup_path, preview_file->source_path, NULL
            );
            uv_fs_req_cleanup(&request);
            if (result == 0) file->source_moved = false;
            else {
                logf_error(
                    "failed to restore patch backup %s to %s",
                    file->backup_path, preview_file->source_path
                );
                succeed = false;
            }
        }
        if (!file->source_moved && file->backup_dir != NULL &&
            !patch_commit_close_backup(loop, file)) {
            logf_error("failed to remove rolled back patch directory: %s", file->backup_dir);
            succeed = false;
        }
    }
    return succeed;
}

static void patch_commit_cleanup_backups(
    uv_loop_t *loop,
    PatchCommit *commit,
    size_t file_count
) {
    size_t index;

    /* Requested files are already committed, so cleanup failure is evidence
     * to log rather than a reason to misreport the patch as failed. */
    for (index = 0; index < file_count; index++) {
        PatchCommitFile *file = &commit->files[index];

        if (file->source_moved) {
            if (patch_commit_remove_owned(loop, file->backup_path) == 0) {
                file->source_moved = false;
            } else {
                logf_error("failed to clean committed patch backup: %s", file->backup_path);
            }
        }
        if (!file->source_moved && file->backup_dir != NULL &&
            !patch_commit_close_backup(loop, file)) {
            logf_error("failed to clean committed patch directory: %s", file->backup_dir);
        }
    }
}

static void patch_commit_release(PatchCommit *commit, size_t file_count) {
    size_t index;

    for (index = 0; index < commit->created_count; index++) {
        free(commit->created_directories[index]);
    }
    for (index = 0; commit->files != NULL && index < file_count; index++) {
        PatchCommitFile *file = &commit->files[index];

        free(file->staged_path);
        free(file->backup_dir);
        free(file->backup_path);
    }
    free(commit->created_directories);
    free(commit->files);
    memset(commit, 0, sizeof(*commit));
}

PatchCommitResult patch_preview_commit(
    uv_loop_t *loop,
    const PatchPreview *preview
) {
    PatchCommit commit = {0};
    size_t index;

    if (!patch_created_capacity(preview, &commit.created_capacity) ||
        preview->file_count > SIZE_MAX / sizeof(*commit.files) ||
        commit.created_capacity > SIZE_MAX / sizeof(*commit.created_directories)) {
        return PATCH_COMMIT_FAILED;
    }
    commit.files = calloc(preview->file_count, sizeof(*commit.files));
    if (commit.files == NULL) return PATCH_COMMIT_FAILED;
    if (commit.created_capacity != 0) {
        commit.created_directories = calloc(
            commit.created_capacity, sizeof(*commit.created_directories)
        );
        if (commit.created_directories == NULL) {
            patch_commit_release(&commit, preview->file_count);
            return PATCH_COMMIT_FAILED;
        }
    }
    for (index = 0; index < preview->file_count; index++) {
        const char *target = preview->files[index].target_path;

        if (target != NULL &&
            (!patch_commit_make_parents(loop, preview, &commit, target) ||
                !patch_commit_stage_file(
                    loop, &preview->files[index], &commit.files[index]
                ))) {
            patch_commit_cleanup_staged(loop, &commit, preview->file_count);
            patch_commit_cleanup_directories(loop, &commit);
            patch_commit_release(&commit, preview->file_count);
            return PATCH_COMMIT_FAILED;
        }
    }
    if (!patch_preview_validate(loop, preview)) {
        patch_commit_cleanup_staged(loop, &commit, preview->file_count);
        patch_commit_cleanup_directories(loop, &commit);
        patch_commit_release(&commit, preview->file_count);
        return PATCH_COMMIT_FAILED;
    }
    for (index = 0; index < preview->file_count; index++) {
        if (!patch_commit_file(loop, &preview->files[index], &commit.files[index])) {
            bool rolled_back = patch_commit_rollback(loop, preview, &commit);

            patch_commit_cleanup_staged(loop, &commit, preview->file_count);
            patch_commit_cleanup_directories(loop, &commit);
            patch_commit_release(&commit, preview->file_count);
            return rolled_back ? PATCH_COMMIT_FAILED : PATCH_COMMIT_UNCERTAIN;
        }
    }
    patch_commit_cleanup_backups(loop, &commit, preview->file_count);
    /* Newly created parents now contain committed targets and must remain. */
    patch_commit_release(&commit, preview->file_count);
    return PATCH_COMMIT_SUCCEEDED;
}
