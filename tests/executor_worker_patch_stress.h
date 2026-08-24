#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_STRESS_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_STRESS_H

#include <stdbool.h>
#include <uv.h>

bool patch_test_run_stress(uv_loop_t *loop, bool inject_faults);
bool patch_test_run_five_minute_stress(uv_loop_t *loop);

#endif
