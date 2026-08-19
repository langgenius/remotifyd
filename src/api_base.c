#include "api_base.h"

#include "consts.h"
#include "logger.h"
#include "state.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static char *api_base;

static bool init_api_base(void) {
    State state = {0};

    if (!state_load(&state)) return false;

    if (state.api_base[0] == '\0') {
        free(state.api_base);
        state.api_base = malloc(sizeof(remotify_api_base));

        if (state.api_base == NULL) {
            state_free(&state);
            return false;
        }

        memcpy(state.api_base, remotify_api_base, sizeof(remotify_api_base));
    }

    /* The process cache owns the loaded value and avoids repeated file reads. */
    api_base = state.api_base;
    state.api_base = NULL;
    state_free(&state);
    return true;
}

#if defined(_MSC_VER)

static void __cdecl api_base_pre_hook(void);

#pragma section(".CRT$XCU", read)

__declspec(allocate(".CRT$XCU"))
void (__cdecl *api_base_pre_hook_registration)(void) = api_base_pre_hook;

static void __cdecl api_base_pre_hook(void) {
    if (!init_api_base()) {
        logf_error("failed to initialize API base");
        exit(EXIT_FAILURE);
    }
}

#elif defined(__GNUC__) || defined(__clang__)

static void api_base_pre_hook(void) __attribute__((constructor));

static void api_base_pre_hook(void) {
    if (!init_api_base()) {
        logf_error("failed to initialize API base");
        exit(EXIT_FAILURE);
    }
}

#else
#error "remotifyd requires compiler pre-hook support"
#endif

const char *get_api_base(void) {
    return api_base;
}
