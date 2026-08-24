#ifdef uv_fs_open
#undef uv_fs_open
#endif
#ifdef uv_fs_write
#undef uv_fs_write
#endif
#ifdef uv_fs_fsync
#undef uv_fs_fsync
#endif
#ifdef uv_fs_fstat
#undef uv_fs_fstat
#endif
#ifdef uv_fs_rename
#undef uv_fs_rename
#endif
#ifdef uv_fs_chmod
#undef uv_fs_chmod
#endif
#ifdef uv_fs_unlink
#undef uv_fs_unlink
#endif

#include <uv.h>

#include "executor_worker_patch_fault.h"

#include <stdint.h>
#include <string.h>

static bool patch_fault_armed;
static bool patch_fault_injected;
static bool patch_fault_planned;
static PatchTestFaultOperation patch_fault_target;
static PatchTestFaultOperation patch_fault_secondary;
static PatchTestFaultReport patch_fault_report;
static uint32_t patch_fault_states[PATCH_TEST_FAULT_OPERATION_COUNT];
static size_t patch_fault_failures[PATCH_TEST_FAULT_OPERATION_COUNT];

static bool patch_fault_next(uint32_t *state, uint32_t denominator) {
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state % denominator == 0;
}

static bool patch_fault_should_fail(
    PatchTestFaultOperation operation,
    uint32_t denominator,
    bool private_cleanup
) {
    if (!patch_fault_armed) return false;
    if (patch_fault_planned) {
        if (operation != patch_fault_target) return false;
        patch_fault_target = patch_fault_secondary;
        patch_fault_secondary = PATCH_TEST_FAULT_NONE;
    } else if (patch_fault_injected ||
        !patch_fault_next(&patch_fault_states[operation], denominator)) return false;
    patch_fault_injected = true;
    patch_fault_report.operation = operation;
    patch_fault_report.count++;
    patch_fault_report.private_cleanup |= private_cleanup;
    patch_fault_failures[operation]++;
    return true;
}

static bool patch_fault_private_path(const char *path) {
    static const char prefix[] = ".remotify-patch-";
    const char *match = path;

    while ((match = strstr(match, prefix)) != NULL) {
        if (match == path || match[-1] == '/' || match[-1] == '\\') return true;
        match++;
    }
    return false;
}

static int patch_fault_request(uv_loop_t *loop, uv_fs_t *request) {
    /* The dummy sync request only establishes libuv cleanup ownership. */
    (void)uv_fs_stat(loop, request, "", NULL);
    request->result = UV_EIO;
    return UV_EIO;
}

void patch_test_fault_reset(void) {
    size_t index;

    patch_fault_armed = false;
    patch_fault_injected = false;
    patch_fault_planned = false;
    memset(&patch_fault_report, 0, sizeof(patch_fault_report));
    for (index = 0; index < PATCH_TEST_FAULT_OPERATION_COUNT; index++) {
        patch_fault_states[index] = UINT32_C(0x91e10da5) ^
            (uint32_t)index * UINT32_C(0x9e3779b9);
        patch_fault_failures[index] = 0;
    }
}

void patch_test_fault_arm(void) {
    memset(&patch_fault_report, 0, sizeof(patch_fault_report));
    patch_fault_injected = false;
    patch_fault_planned = false;
    patch_fault_armed = true;
}

void patch_test_fault_arm_plan(
    PatchTestFaultOperation first,
    PatchTestFaultOperation second
) {
    patch_test_fault_arm();
    patch_fault_planned = true;
    patch_fault_target = first;
    patch_fault_secondary = second;
}

PatchTestFaultReport patch_test_fault_disarm(void) {
    patch_fault_armed = false;
    return patch_fault_report;
}

void patch_test_fault_counts(size_t counts[PATCH_TEST_FAULT_OPERATION_COUNT]) {
    memcpy(counts, patch_fault_failures, sizeof(patch_fault_failures));
}

int patch_test_uv_fs_open(
    uv_loop_t *loop,
    uv_fs_t *request,
    const char *path,
    int flags,
    int mode,
    uv_fs_cb callback
) {
    if (callback == NULL &&
        patch_fault_should_fail(PATCH_TEST_FAULT_OPEN, 7, false)) {
        return patch_fault_request(loop, request);
    }
    return uv_fs_open(loop, request, path, flags, mode, callback);
}

int patch_test_uv_fs_write(
    uv_loop_t *loop,
    uv_fs_t *request,
    uv_file file,
    const uv_buf_t buffers[],
    unsigned int buffer_count,
    int64_t offset,
    uv_fs_cb callback
) {
    if (callback == NULL &&
        patch_fault_should_fail(PATCH_TEST_FAULT_WRITE, 5, false)) {
        return patch_fault_request(loop, request);
    }
    return uv_fs_write(
        loop, request, file, buffers, buffer_count, offset, callback
    );
}

int patch_test_uv_fs_fsync(
    uv_loop_t *loop,
    uv_fs_t *request,
    uv_file file,
    uv_fs_cb callback
) {
    if (callback == NULL &&
        patch_fault_should_fail(PATCH_TEST_FAULT_FSYNC, 5, false)) {
        return patch_fault_request(loop, request);
    }
    return uv_fs_fsync(loop, request, file, callback);
}

int patch_test_uv_fs_fstat(
    uv_loop_t *loop,
    uv_fs_t *request,
    uv_file file,
    uv_fs_cb callback
) {
    if (callback == NULL &&
        patch_fault_should_fail(PATCH_TEST_FAULT_FSTAT, 3, false)) {
        return patch_fault_request(loop, request);
    }
    return uv_fs_fstat(loop, request, file, callback);
}

int patch_test_uv_fs_rename(
    uv_loop_t *loop,
    uv_fs_t *request,
    const char *path,
    const char *new_path,
    uv_fs_cb callback
) {
    if (callback == NULL &&
        patch_fault_should_fail(PATCH_TEST_FAULT_RENAME, 7, false)) {
        return patch_fault_request(loop, request);
    }
    return uv_fs_rename(loop, request, path, new_path, callback);
}

int patch_test_uv_fs_chmod(
    uv_loop_t *loop,
    uv_fs_t *request,
    const char *path,
    int mode,
    uv_fs_cb callback
) {
    bool private_cleanup = patch_fault_private_path(path);

    if (callback == NULL &&
        patch_fault_should_fail(PATCH_TEST_FAULT_CHMOD, 7, private_cleanup)) {
        return patch_fault_request(loop, request);
    }
    return uv_fs_chmod(loop, request, path, mode, callback);
}

int patch_test_uv_fs_unlink(
    uv_loop_t *loop,
    uv_fs_t *request,
    const char *path,
    uv_fs_cb callback
) {
    bool private_cleanup = patch_fault_private_path(path);

    if (callback == NULL &&
        patch_fault_should_fail(PATCH_TEST_FAULT_UNLINK, 5, private_cleanup)) {
        return patch_fault_request(loop, request);
    }
    return uv_fs_unlink(loop, request, path, callback);
}
