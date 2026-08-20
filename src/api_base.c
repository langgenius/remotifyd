/*
 * This module is relatively independent and its function is simple, so almost
 * all of this implementation was generated directly by GPT-5.6 Sol without a
 * detailed system design. It should be reviewed and optimized later.
 */
#include "api_base.h"

#include "consts.h"
#include "logger.h"
#include "state.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *api_base;
static char *websocket_url;

static bool init_api_base(void) {
    State state = {0};
    const char *websocket_scheme;
    const char *base_without_scheme;
    size_t websocket_url_length;
    bool trailing_slash;

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

    if (strncmp(state.api_base, "https://", sizeof("https://") - 1) == 0) {
        websocket_scheme = "wss://";
        base_without_scheme = state.api_base + sizeof("https://") - 1;
    } else if (strncmp(state.api_base, "http://", sizeof("http://") - 1) == 0) {
        websocket_scheme = "ws://";
        base_without_scheme = state.api_base + sizeof("http://") - 1;
    } else {
        state_free(&state);
        return false;
    }

    trailing_slash = state.api_base[strlen(state.api_base) - 1] == '/';
    websocket_url_length = strlen(websocket_scheme) +
        strlen(base_without_scheme) +
        (trailing_slash ? 0 : 1) +
        sizeof("device-connections");
    websocket_url = malloc(websocket_url_length);

    if (websocket_url == NULL) {
        state_free(&state);
        return false;
    }

    /*
     * The WebSocket URL is a computed view of the API base. Deriving it here
     * keeps scheme mapping and endpoint layout out of the connection loop.
     */
    sprintf(
        websocket_url,
        "%s%s%sdevice-connections",
        websocket_scheme,
        base_without_scheme,
        trailing_slash ? "" : "/"
    );

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

const char *get_websocket_url(void) {
    return websocket_url;
}
