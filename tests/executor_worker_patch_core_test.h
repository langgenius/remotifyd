#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_CORE_TEST_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_CORE_TEST_H

#include <stdbool.h>
#include <stddef.h>

typedef struct EchoUnifiedPath {
    const char *old_path_base64;
    const char *new_path_base64;
} EchoUnifiedPath;

typedef struct EchoUnifiedDiffCase {
    const char *name_base64;
    const char *patch_base64;
    const EchoUnifiedPath *paths;
    size_t path_count;
    const char *const *first_hunk_lines_base64;
    size_t first_hunk_line_count;
    size_t expected_old_start;
    size_t expected_old_lines;
    size_t expected_new_lines;
    bool check_old_start;
    bool check_old_lines;
    bool check_new_lines;
    const char *const *format_contains_base64;
    size_t format_contains_count;
    bool format_twice_equal;
    const char *normalized_header_base64;
    bool parse_before_normalization_fails;
} EchoUnifiedDiffCase;

extern const EchoUnifiedDiffCase echo_unified_diff_cases[];
extern const size_t echo_unified_diff_case_count;
bool run_patch_core_tests(void);

#endif
