#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_HOST_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <uv.h>

typedef enum PatchTestHostFaultMode {
    PATCH_TEST_HOST_FAULT_NONE,
    PATCH_TEST_HOST_FAULT_RANDOM,
    PATCH_TEST_HOST_FAULT_ROLLBACK_UNLINK,
    PATCH_TEST_HOST_FAULT_ROLLBACK_RENAME
} PatchTestHostFaultMode;

size_t patch_test_host_case_count(void);
bool patch_test_run_host_case(
    uv_loop_t *loop,
    size_t index,
    PatchTestHostFaultMode fault_mode
);

#endif
