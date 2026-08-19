#include "executor_worker_shell_locate.h"

#include "logger.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

static char *shell_path;

const char *executor_worker_shell_locate(void) {
    if (shell_path != NULL) return shell_path;

#ifdef _WIN32
    char located[MAX_PATH];
    DWORD length = SearchPathA(NULL, "powershell.exe", NULL, MAX_PATH, located, NULL);

    if (length == 0 || length >= MAX_PATH) {
        length = SearchPathA(NULL, "cmd.exe", NULL, MAX_PATH, located, NULL);
    }

    if (length == 0 || length >= MAX_PATH) {
        logf_error("neither powershell.exe nor cmd.exe could be found");
        return NULL;
    }

    shell_path = malloc((size_t)length + 1);

    if (shell_path == NULL) {
        logf_error("failed to cache shell path");
        return NULL;
    }

    memcpy(shell_path, located, (size_t)length + 1);
#else
    static const char shell[] = "/bin/sh";

    if (access(shell, X_OK) != 0) {
        logf_error("/bin/sh could not be found or is not executable");
        return NULL;
    }

    shell_path = malloc(sizeof(shell));

    if (shell_path == NULL) {
        logf_error("failed to cache shell path");
        return NULL;
    }

    memcpy(shell_path, shell, sizeof(shell));
#endif

    /* The cached allocation lives for the complete Executor worker process. */
    return shell_path;
}
