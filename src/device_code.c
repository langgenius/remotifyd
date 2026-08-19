#include "device_code.h"

#include "logger.h"
#include "state.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

static char *device_code;

static char *device_code_generate(void) {
    static const char hexadecimal[] = "0123456789abcdef";
    char hostname[UV_MAXHOSTNAMESIZE];
    char slug[33] = {0};
    uint8_t random[12];
    size_t hostname_length = sizeof(hostname);
    size_t slug_length = 0;
    size_t index;
    size_t offset;
    bool separator = true;
    char *result;

    if (uv_os_gethostname(hostname, &hostname_length) != 0 ||
        uv_random(NULL, NULL, random, sizeof(random), 0, NULL) != 0) {
        return NULL;
    }

    for (index = 0;
         hostname[index] != '\0' && slug_length < sizeof(slug) - 1;
         index++) {
        unsigned char character = (unsigned char)tolower((unsigned char)hostname[index]);

        if (isalnum(character)) {
            slug[slug_length++] = (char)character;
            separator = false;
        } else if (!separator) {
            slug[slug_length++] = '-';
            separator = true;
        }
    }

    while (slug_length != 0 && slug[slug_length - 1] == '-') {
        slug[--slug_length] = '\0';
    }

    if (slug_length == 0) memcpy(slug, "device", sizeof("device"));

    result = malloc(strlen("cli--") + strlen(slug) + sizeof(random) * 2 + 1);

    if (result == NULL) return NULL;

    sprintf(result, "cli-%s-", slug);
    offset = strlen(result);

    for (index = 0; index < sizeof(random); index++) {
        result[offset + index * 2] = hexadecimal[random[index] >> 4];
        result[offset + index * 2 + 1] = hexadecimal[random[index] & 15];
    }

    result[offset + sizeof(random) * 2] = '\0';
    return result;
}

static bool device_code_initialize(void) {
    State state = {0};

    if (!state_load(&state)) return false;

    if (state.device_code[0] == '\0') {
        free(state.device_code);
        state.device_code = device_code_generate();

        if (state.device_code == NULL || !state_save(&state)) {
            state_free(&state);
            return false;
        }
    }

    /* The process cache takes ownership of the string loaded from State. */
    device_code = state.device_code;
    state.device_code = NULL;
    state_free(&state);
    return true;
}

#if defined(_MSC_VER)

static void __cdecl device_code_pre_hook(void);

#pragma section(".CRT$XCU", read)

__declspec(allocate(".CRT$XCU"))
void (__cdecl *device_code_pre_hook_registration)(void) = device_code_pre_hook;

static void __cdecl device_code_pre_hook(void) {
    if (!device_code_initialize()) {
        logf_error("failed to initialize device code");
        exit(EXIT_FAILURE);
    }
}

#elif defined(__GNUC__) || defined(__clang__)

static void device_code_pre_hook(void) __attribute__((constructor));

static void device_code_pre_hook(void) {
    if (!device_code_initialize()) {
        logf_error("failed to initialize device code");
        exit(EXIT_FAILURE);
    }
}

#else
#error "remotifyd requires compiler pre-hook support"
#endif

const char *device_code_get(void) {
    return device_code;
}
