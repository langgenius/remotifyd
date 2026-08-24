#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_FAULT_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_FAULT_H

#include <stdbool.h>
#include <stddef.h>
#include <uv.h>

typedef enum PatchTestFaultOperation {
    PATCH_TEST_FAULT_NONE,
    PATCH_TEST_FAULT_OPEN,
    PATCH_TEST_FAULT_WRITE,
    PATCH_TEST_FAULT_FSYNC,
    PATCH_TEST_FAULT_FSTAT,
    PATCH_TEST_FAULT_RENAME,
    PATCH_TEST_FAULT_CHMOD,
    PATCH_TEST_FAULT_UNLINK,
    PATCH_TEST_FAULT_OPERATION_COUNT
} PatchTestFaultOperation;

typedef struct PatchTestFaultReport {
    /* A plan injects first then second; operation is the last injected call,
     * while private_cleanup is the OR across every injected call. */
    PatchTestFaultOperation operation;
    size_t count;
    bool private_cleanup;
} PatchTestFaultReport;

void patch_test_fault_reset(void);
void patch_test_fault_arm(void);
void patch_test_fault_arm_plan(
    PatchTestFaultOperation first,
    PatchTestFaultOperation second
);
PatchTestFaultReport patch_test_fault_disarm(void);
void patch_test_fault_counts(size_t counts[PATCH_TEST_FAULT_OPERATION_COUNT]);

int patch_test_uv_fs_open(
    uv_loop_t *loop,
    uv_fs_t *request,
    const char *path,
    int flags,
    int mode,
    uv_fs_cb callback
);
int patch_test_uv_fs_write(
    uv_loop_t *loop,
    uv_fs_t *request,
    uv_file file,
    const uv_buf_t buffers[],
    unsigned int buffer_count,
    int64_t offset,
    uv_fs_cb callback
);
int patch_test_uv_fs_fsync(
    uv_loop_t *loop,
    uv_fs_t *request,
    uv_file file,
    uv_fs_cb callback
);
int patch_test_uv_fs_fstat(
    uv_loop_t *loop,
    uv_fs_t *request,
    uv_file file,
    uv_fs_cb callback
);
int patch_test_uv_fs_rename(
    uv_loop_t *loop,
    uv_fs_t *request,
    const char *path,
    const char *new_path,
    uv_fs_cb callback
);
int patch_test_uv_fs_chmod(
    uv_loop_t *loop,
    uv_fs_t *request,
    const char *path,
    int mode,
    uv_fs_cb callback
);
int patch_test_uv_fs_unlink(
    uv_loop_t *loop,
    uv_fs_t *request,
    const char *path,
    uv_fs_cb callback
);

#endif
