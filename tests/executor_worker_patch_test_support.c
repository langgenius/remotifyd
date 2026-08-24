#include "executor_worker_patch_test_support.h"

#include <stdlib.h>
#include <string.h>

static int base64_value(char value) {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}

uint8_t *patch_test_decode_base64(const char *input, size_t *length) {
    size_t input_length = strlen(input);
    uint8_t *output = malloc(input_length / 4 * 3 + 1);
    size_t input_cursor = 0;
    size_t output_cursor = 0;

    if (output == NULL) return NULL;
    while (input_cursor < input_length) {
        int values[4];
        size_t index;

        for (index = 0; index < 4; index++) {
            char value = input[input_cursor++];
            values[index] = value == '=' ? -2 : base64_value(value);
            if (values[index] == -1) {
                free(output);
                return NULL;
            }
        }
        output[output_cursor++] = (uint8_t)((values[0] << 2) | (values[1] >> 4));
        if (values[2] != -2) {
            output[output_cursor++] = (uint8_t)((values[1] << 4) | (values[2] >> 2));
        }
        if (values[3] != -2) {
            output[output_cursor++] = (uint8_t)((values[2] << 6) | values[3]);
        }
    }
    output[output_cursor] = 0;
    *length = output_cursor;
    return output;
}
