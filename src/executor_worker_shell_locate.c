#include "executor_worker_shell_locate.h"

#include "logger.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static char *shell_path;
static ExecutorShellType shell_type;

static bool executor_worker_shell_locate(void) {
#ifdef _WIN32
    char located[MAX_PATH];
    DWORD length;

    length = SearchPathA(NULL, "powershell.exe", NULL, MAX_PATH, located, NULL);

    if (length != 0 && length < MAX_PATH) {
        shell_type = EXECUTOR_SHELL_POWERSHELL;
    } else {
        length = SearchPathA(NULL, "cmd.exe", NULL, MAX_PATH, located, NULL);

        if (length == 0 || length >= MAX_PATH) {
            logf_error("neither powershell.exe nor cmd.exe could be found");
            return false;
        }

        shell_type = EXECUTOR_SHELL_CMD;
    }

    shell_path = malloc((size_t)length + 1);

    if (shell_path == NULL) {
        logf_error("failed to cache shell path");
        return false;
    }

    memcpy(shell_path, located, (size_t)length + 1);
#else
    static const char shell[] = "/bin/sh";

    if (access(shell, X_OK) != 0) {
        logf_error("/bin/sh could not be found or is not executable");
        return false;
    }

    shell_path = malloc(sizeof(shell));

    if (shell_path == NULL) {
        logf_error("failed to cache shell path");
        return false;
    }

    memcpy(shell_path, shell, sizeof(shell));
    shell_type = EXECUTOR_SHELL_SH;
#endif

    return true;
}

#if defined(_MSC_VER)

static void __cdecl executor_worker_shell_pre_hook(void);

#pragma section(".CRT$XCU", read)

__declspec(allocate(".CRT$XCU"))
void (__cdecl *executor_worker_shell_pre_hook_registration)(void) = executor_worker_shell_pre_hook;

static void __cdecl executor_worker_shell_pre_hook(void) {
    /* Each process must finish shell discovery before it can enter main. */
    if (!executor_worker_shell_locate()) exit(EXIT_FAILURE);
}

#elif defined(__GNUC__) || defined(__clang__)

static void executor_worker_shell_pre_hook(void) __attribute__((constructor));

static void executor_worker_shell_pre_hook(void) {
    /* Each process must finish shell discovery before it can enter main. */
    if (!executor_worker_shell_locate()) exit(EXIT_FAILURE);
}

#else
#error "remotifyd requires compiler pre-hook support"
#endif

const char *executor_worker_shell_path(void) {
    return shell_path;
}

ExecutorShellType executor_worker_shell_type(void) {
    return shell_type;
}
