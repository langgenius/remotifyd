#include "install/state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool state_resolve_directory(const char *explicit_directory, char **directory) {
    const char *base;
    const char *suffix;
    size_t length;

    if (explicit_directory != NULL && explicit_directory[0] != '\0') {
        *directory = strdup(explicit_directory);
        return *directory != NULL;
    }
#if defined(__APPLE__)
    base = getenv("HOME");
    suffix = "/Library/Application Support/remotify/daemon";
#else
    base = getenv("XDG_CONFIG_HOME");
    suffix = "/remotify/daemon";
    if (base == NULL || base[0] == '\0') {
        base = getenv("HOME");
        suffix = "/.config/remotify/daemon";
    }
#endif
    if (base == NULL || base[0] == '\0') {
        return false;
    }
    length = strlen(base) + strlen(suffix) + 1;
    *directory = malloc(length);
    if (*directory != NULL) {
        snprintf(*directory, length, "%s%s", base, suffix);
    }
    return *directory != NULL;
}
