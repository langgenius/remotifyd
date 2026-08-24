#include "executor_worker_patch_stress.h"

#include "executor_worker_patch_fault.h"
#include "executor_worker_patch_host.h"

#include <stdint.h>
#include <stdio.h>

bool patch_test_run_stress(uv_loop_t *loop, bool inject_faults) {
    const size_t case_count = patch_test_host_case_count();
    const size_t iterations = 4096;
    size_t index;

    for (index = 0; index < iterations; index++) {
        PatchTestHostFaultMode fault_mode = inject_faults ?
            PATCH_TEST_HOST_FAULT_RANDOM : PATCH_TEST_HOST_FAULT_NONE;

        if (!patch_test_run_host_case(loop, index % case_count, fault_mode)) {
            fprintf(stderr, "patch stress iteration %zu failed\n", index + 1);
            return false;
        }
    }
    if (inject_faults) {
        size_t failures[PATCH_TEST_FAULT_OPERATION_COUNT];
        size_t operation;

        patch_test_fault_counts(failures);
        for (operation = PATCH_TEST_FAULT_OPEN;
            operation < PATCH_TEST_FAULT_OPERATION_COUNT; operation++) {
            if (failures[operation] == 0) return false;
        }
        if (!patch_test_run_host_case(
            loop, 0, PATCH_TEST_HOST_FAULT_ROLLBACK_UNLINK
        ) || !patch_test_run_host_case(
            loop, 0, PATCH_TEST_HOST_FAULT_ROLLBACK_RENAME
        )) return false;
        printf("all %zu patch fault iterations passed "
            "(%zu open, %zu write, %zu fsync, %zu fstat, %zu rename, "
            "%zu chmod, %zu unlink; 2 rollback failures)\n",
            iterations, failures[PATCH_TEST_FAULT_OPEN],
            failures[PATCH_TEST_FAULT_WRITE], failures[PATCH_TEST_FAULT_FSYNC],
            failures[PATCH_TEST_FAULT_FSTAT], failures[PATCH_TEST_FAULT_RENAME],
            failures[PATCH_TEST_FAULT_CHMOD], failures[PATCH_TEST_FAULT_UNLINK]);
    } else {
        printf("all %zu patch stress iterations passed\n", iterations);
    }
    return true;
}

bool patch_test_run_five_minute_stress(uv_loop_t *loop) {
    const uint64_t duration = UINT64_C(300) * UINT64_C(1000000000);
    const size_t case_count = patch_test_host_case_count();
    const uint64_t start = uv_hrtime();
    uint64_t elapsed;
    size_t iterations = 0;

    do {
        if (!patch_test_run_host_case(
            loop, iterations % case_count, PATCH_TEST_HOST_FAULT_NONE
        )) {
            fprintf(stderr, "five-minute patch stress iteration %zu failed\n",
                iterations + 1);
            return false;
        }
        iterations++;
        elapsed = uv_hrtime() - start;
    } while (elapsed < duration);
    printf("all %zu five-minute patch stress iterations passed in %llu ms\n",
        iterations, (unsigned long long)(elapsed / UINT64_C(1000000)));
    return true;
}
