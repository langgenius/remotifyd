#ifndef REMOTIFY_EXECUTOR_WORKER_PATCH_TEST_SUPPORT_H
#define REMOTIFY_EXECUTOR_WORKER_PATCH_TEST_SUPPORT_H

#include <stddef.h>
#include <stdint.h>

uint8_t *patch_test_decode_base64(const char *input, size_t *length);

#endif
