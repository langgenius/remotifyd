#include "executor_worker.h"

#include "random.h"
#include "executor_worker_patch_core_test.h"
#include "executor_worker_patch_fault.h"
#include "executor_worker_patch_host.h"
#include "executor_worker_patch_stress.h"
#include "executor_worker_patch_test_support.h"

#include <stdbool.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct EchoPatchEntry {
    const char *path_base64;
    const char *content_base64;
    int mode;
    bool directory;
} EchoPatchEntry;

typedef struct EchoPatchCase {
    int id;
    const char *name_base64;
    const char *cwd_base64;
    const char *patch_base64;
    bool expected_ok;
    const EchoPatchEntry *before;
    size_t before_count;
    const EchoPatchEntry *after;
    size_t after_count;
} EchoPatchCase;

#include "echo_patch_cases.inc"

static char *join_path(const char *left, const char *right) {
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    char *path = malloc(left_length + right_length + 2);

    if (path == NULL) return NULL;
    memcpy(path, left, left_length);
    path[left_length] = '/';
    memcpy(path + left_length + 1, right, right_length + 1);
    return path;
}

static char *copy_string(const char *value) {
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if (copy != NULL) memcpy(copy, value, length + 1);
    return copy;
}

static int fs_mkdir(uv_loop_t *loop, const char *path, int mode) {
    uv_fs_t request;
    int result = uv_fs_mkdir(loop, &request, path, mode, NULL);
    uv_fs_req_cleanup(&request);
    return result;
}

static int fs_stat(uv_loop_t *loop, const char *path, uv_stat_t *stat_buffer) {
    uv_fs_t request;
    int result = uv_fs_stat(loop, &request, path, NULL);
    if (result == 0) *stat_buffer = request.statbuf;
    uv_fs_req_cleanup(&request);
    return result;
}

static bool make_parents(uv_loop_t *loop, const char *path) {
    char *copy = copy_string(path);
    size_t index;

    if (copy == NULL) return false;
    for (index = 1; copy[index] != 0; index++) {
        int result;
        if (copy[index] != '/' && copy[index] != '\\') continue;
        copy[index] = 0;
        result = fs_mkdir(loop, copy, 0777);
        copy[index] = '/';
        if (result != 0 && result != UV_EEXIST) {
            free(copy);
            return false;
        }
    }
    free(copy);
    return true;
}

static bool write_file(
    uv_loop_t *loop,
    const char *path,
    const uint8_t *bytes,
    size_t length,
    int mode
) {
    uv_fs_t request;
    uv_file file;
    size_t written = 0;
    int result;

    if (!make_parents(loop, path)) return false;
    result = uv_fs_open(loop, &request, path, O_CREAT | O_TRUNC | O_WRONLY, mode, NULL);
    file = result;
    uv_fs_req_cleanup(&request);
    if (result < 0) return false;

    while (written < length) {
        uv_buf_t buffer = uv_buf_init((char *)bytes + written, (unsigned int)(length - written));
        result = uv_fs_write(loop, &request, file, &buffer, 1, (int64_t)written, NULL);
        uv_fs_req_cleanup(&request);
        if (result <= 0) break;
        written += (size_t)result;
    }
    result = uv_fs_close(loop, &request, file, NULL);
    uv_fs_req_cleanup(&request);
    return result == 0 && written == length;
}

static char *make_root(uv_loop_t *loop) {
    static const char hexadecimal[] = "0123456789abcdef";
    uint8_t random[16];
    size_t temporary_length = 4096;
    char temporary[4096];
    char name[33];
    char *root;
    size_t index;

    if (uv_os_tmpdir(temporary, &temporary_length) != 0 ||
        !random_bytes(random, sizeof(random))) return NULL;
    for (index = 0; index < sizeof(random); index++) {
        name[index * 2] = hexadecimal[random[index] >> 4];
        name[index * 2 + 1] = hexadecimal[random[index] & 15];
    }
    name[32] = 0;
    root = join_path(temporary, name);
    if (root == NULL || fs_mkdir(loop, root, 0700) != 0) {
        free(root);
        return NULL;
    }
    return root;
}

static bool create_entries(
    uv_loop_t *loop,
    const char *root,
    const EchoPatchEntry *entries,
    size_t count
) {
    size_t index;

    for (index = 0; index < count; index++) {
        const EchoPatchEntry *entry = &entries[index];
        size_t path_length;
        uint8_t *relative = patch_test_decode_base64(entry->path_base64, &path_length);
        char *path;

        if (relative == NULL) return false;
        path = join_path(root, (const char *)relative);
        free(relative);
        if (path == NULL) return false;

        if (entry->directory) {
            if (!make_parents(loop, path) || fs_mkdir(loop, path, entry->mode) != 0) {
                free(path);
                return false;
            }
        } else {
            size_t content_length;
            uint8_t *content = patch_test_decode_base64(entry->content_base64, &content_length);
            bool succeed = content != NULL &&
                write_file(loop, path, content, content_length, entry->mode);
            free(content);
            if (!succeed) {
                free(path);
                return false;
            }
        }
        free(path);
    }
    return true;
}

static bool read_file(
    uv_loop_t *loop,
    const char *path,
    uint8_t **bytes,
    size_t *length
) {
    uv_fs_t request;
    uv_stat_t stat_buffer;
    uv_file file;
    size_t received = 0;
    int result;

    if (fs_stat(loop, path, &stat_buffer) != 0 || stat_buffer.st_size < 0) return false;
    *length = (size_t)stat_buffer.st_size;
    *bytes = malloc(*length == 0 ? 1 : *length);
    if (*bytes == NULL) return false;
    result = uv_fs_open(loop, &request, path, O_RDONLY, 0, NULL);
    file = result;
    uv_fs_req_cleanup(&request);
    if (result < 0) return false;
    while (received < *length) {
        uv_buf_t buffer = uv_buf_init((char *)*bytes + received, (unsigned int)(*length - received));
        result = uv_fs_read(loop, &request, file, &buffer, 1, (int64_t)received, NULL);
        uv_fs_req_cleanup(&request);
        if (result <= 0) break;
        received += (size_t)result;
    }
    result = uv_fs_close(loop, &request, file, NULL);
    uv_fs_req_cleanup(&request);
    return result == 0 && received == *length;
}

static bool private_patch_entry(const char *name) {
    static const char prefix[] = ".remotify-patch-";
    return strncmp(name, prefix, sizeof(prefix) - 1) == 0;
}

static size_t count_entries(
    uv_loop_t *loop,
    const char *directory,
    bool ignore_private,
    size_t *private_count
) {
    uv_fs_t request;
    uv_dirent_t entry;
    size_t count = 0;
    int result = uv_fs_scandir(loop, &request, directory, 0, NULL);

    if (result < 0) {
        uv_fs_req_cleanup(&request);
        return SIZE_MAX;
    }
    while (uv_fs_scandir_next(&request, &entry) == 0) {
        if (ignore_private && private_patch_entry(entry.name)) {
            (*private_count)++;
            continue;
        }
        char *path = join_path(directory, entry.name);
        size_t child_count;
        if (path == NULL) {
            uv_fs_req_cleanup(&request);
            return SIZE_MAX;
        }
        count++;
        if (entry.type == UV_DIRENT_DIR) {
            child_count = count_entries(loop, path, ignore_private, private_count);
            if (child_count == SIZE_MAX) {
                free(path);
                uv_fs_req_cleanup(&request);
                return SIZE_MAX;
            }
            count += child_count;
        }
        free(path);
    }
    uv_fs_req_cleanup(&request);
    return count;
}

static bool verify_entries(
    uv_loop_t *loop,
    const char *root,
    const EchoPatchEntry *entries,
    size_t count,
    bool ignore_private,
    size_t *private_count
) {
    size_t index;
    size_t ignored = 0;

    if (count_entries(loop, root, ignore_private, &ignored) != count) return false;
    for (index = 0; index < count; index++) {
        const EchoPatchEntry *entry = &entries[index];
        size_t path_length;
        uint8_t *relative = patch_test_decode_base64(entry->path_base64, &path_length);
        char *path;
        uv_stat_t stat_buffer;

        if (relative == NULL) return false;
        path = join_path(root, (const char *)relative);
        free(relative);
        if (path == NULL || fs_stat(loop, path, &stat_buffer) != 0) {
            free(path);
            return false;
        }

        if (entry->directory) {
            if ((stat_buffer.st_mode & S_IFMT) != S_IFDIR) {
                free(path);
                return false;
            }
        } else {
            uint8_t *actual = NULL;
            uint8_t *expected;
            size_t actual_length;
            size_t expected_length;
            bool same;

            expected = patch_test_decode_base64(entry->content_base64, &expected_length);
            same = expected != NULL && read_file(loop, path, &actual, &actual_length) &&
                actual_length == expected_length &&
                memcmp(actual, expected, actual_length) == 0;
            free(actual);
            free(expected);
            if (!same) {
                free(path);
                return false;
            }
        }
#ifndef _WIN32
        if ((stat_buffer.st_mode & 0777) != entry->mode) {
            free(path);
            return false;
        }
#endif
        free(path);
    }
    if (private_count != NULL) *private_count = ignored;
    return true;
}

static bool remove_tree(uv_loop_t *loop, const char *path) {
    uv_fs_t scan;
    uv_dirent_t entry;
    int result = uv_fs_scandir(loop, &scan, path, 0, NULL);

    if (result >= 0) {
        while (uv_fs_scandir_next(&scan, &entry) == 0) {
            char *child = join_path(path, entry.name);
            bool succeed;
            if (child == NULL) {
                uv_fs_req_cleanup(&scan);
                return false;
            }
            if (entry.type == UV_DIRENT_DIR) {
                succeed = remove_tree(loop, child);
            } else {
                uv_fs_t unlink_request;
                succeed = uv_fs_unlink(loop, &unlink_request, child, NULL) == 0;
                uv_fs_req_cleanup(&unlink_request);
            }
            free(child);
            if (!succeed) {
                uv_fs_req_cleanup(&scan);
                return false;
            }
        }
    }
    uv_fs_req_cleanup(&scan);
    {
        uv_fs_t request;
        result = uv_fs_rmdir(loop, &request, path, NULL);
        uv_fs_req_cleanup(&request);
    }
    return result == 0;
}

static bool run_case(
    uv_loop_t *loop,
    const EchoPatchCase *test,
    PatchTestHostFaultMode fault_mode
) {
    char *root = make_root(loop);
    uint8_t *patch;
    uint8_t *cwd_relative;
    char *cwd;
    uint8_t *name;
    size_t patch_length;
    size_t cwd_length;
    size_t name_length;
    IpcStatus status;
    PatchTestFaultReport fault = {0};
    bool succeed;

    if (root == NULL || !create_entries(loop, root, test->before, test->before_count)) return false;
    patch = patch_test_decode_base64(test->patch_base64, &patch_length);
    cwd_relative = patch_test_decode_base64(test->cwd_base64, &cwd_length);
    name = patch_test_decode_base64(test->name_base64, &name_length);
    cwd = cwd_length == 0 ? copy_string(root) : join_path(root, (const char *)cwd_relative);
    if (patch == NULL || cwd_relative == NULL || name == NULL || cwd == NULL) return false;

    if (fault_mode == PATCH_TEST_HOST_FAULT_RANDOM) patch_test_fault_arm();
    else if (fault_mode == PATCH_TEST_HOST_FAULT_ROLLBACK_UNLINK) {
        patch_test_fault_arm_plan(PATCH_TEST_FAULT_CHMOD, PATCH_TEST_FAULT_UNLINK);
    } else if (fault_mode == PATCH_TEST_HOST_FAULT_ROLLBACK_RENAME) {
        patch_test_fault_arm_plan(PATCH_TEST_FAULT_CHMOD, PATCH_TEST_FAULT_RENAME);
    }
    status = executor_worker_patch_apply(
        loop,
        patch,
        patch_length,
        (const uint8_t *)cwd,
        strlen(cwd)
    );
    if (fault_mode != PATCH_TEST_HOST_FAULT_NONE) fault = patch_test_fault_disarm();
    if (fault_mode == PATCH_TEST_HOST_FAULT_RANDOM && fault.private_cleanup) {
        size_t private_count = 0;

        succeed = (status == IPC_STATUS_OK) == test->expected_ok &&
            verify_entries(
                loop, root, test->after, test->after_count, true, &private_count
            ) && private_count != 0;
    } else if (fault_mode == PATCH_TEST_HOST_FAULT_RANDOM && fault.count != 0) {
        succeed = status == IPC_STATUS_FAILED &&
            verify_entries(loop, root, test->before, test->before_count, false, NULL);
    } else if (fault_mode == PATCH_TEST_HOST_FAULT_ROLLBACK_UNLINK ||
        fault_mode == PATCH_TEST_HOST_FAULT_ROLLBACK_RENAME) {
        PatchTestFaultOperation expected =
            fault_mode == PATCH_TEST_HOST_FAULT_ROLLBACK_UNLINK ?
            PATCH_TEST_FAULT_UNLINK : PATCH_TEST_FAULT_RENAME;

        succeed = fault.count == 2 && fault.operation == expected &&
            !fault.private_cleanup && status == IPC_STATUS_UNCERTAIN;
    } else {
        succeed = (status == IPC_STATUS_OK) == test->expected_ok &&
            verify_entries(loop, root, test->after, test->after_count, false, NULL);
    }
    if (!succeed) {
        fprintf(stderr, "case %d failed: %.*s (status %d, expected %d)\n",
            test->id, (int)name_length, name, status, test->expected_ok);
    }

    free(name);
    free(cwd);
    free(cwd_relative);
    free(patch);
    if (!remove_tree(loop, root)) succeed = false;
    free(root);
    return succeed;
}

size_t patch_test_host_case_count(void) {
    return sizeof(echo_patch_cases) / sizeof(echo_patch_cases[0]);
}

bool patch_test_run_host_case(
    uv_loop_t *loop,
    size_t index,
    PatchTestHostFaultMode fault_mode
) {
    if (index >= patch_test_host_case_count()) return false;
    return run_case(loop, &echo_patch_cases[index], fault_mode);
}

int main(int argc, char **argv) {
    uv_loop_t loop;
    size_t index;
    size_t failed = 0;

    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--stress") != 0 &&
        strcmp(argv[1], "--fault-stress") != 0 &&
        strcmp(argv[1], "--stress-five-minutes") != 0)) {
        fprintf(stderr, "usage: %s [--stress|--fault-stress|--stress-five-minutes]\n",
            argv[0]);
        return EXIT_FAILURE;
    }
    if (uv_loop_init(&loop) != 0) return EXIT_FAILURE;
    if (argc == 2) {
        bool five_minutes = strcmp(argv[1], "--stress-five-minutes") == 0;
        bool inject_faults = strcmp(argv[1], "--fault-stress") == 0;
        bool succeed;

        if (inject_faults) patch_test_fault_reset();
        succeed = five_minutes ? patch_test_run_five_minute_stress(&loop) :
            patch_test_run_stress(&loop, inject_faults);
        uv_loop_close(&loop);
        return succeed ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    if (!run_patch_core_tests()) {
        uv_loop_close(&loop);
        return EXIT_FAILURE;
    }
    for (index = 0; index < patch_test_host_case_count(); index++) {
        if (!patch_test_run_host_case(&loop, index, PATCH_TEST_HOST_FAULT_NONE)) failed++;
    }
    uv_loop_close(&loop);
    if (failed != 0) {
        fprintf(stderr, "%zu of %zu Echo patch cases failed\n", failed,
            patch_test_host_case_count());
        return EXIT_FAILURE;
    }
    printf("all %zu Echo patch cases passed\n",
        patch_test_host_case_count());
    return EXIT_SUCCESS;
}
