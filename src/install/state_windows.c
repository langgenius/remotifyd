#include "install/state.h"

#include "fs/windows_utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

bool state_resolve_directory(const char *explicit_directory, char **directory) {
    wchar_t base[32768];
    const char *suffix = "\\remotify\\daemon";
    char *utf8 = NULL;
    size_t length;

    if (explicit_directory != NULL && explicit_directory[0] != '\0') {
        length = strlen(explicit_directory) + 1;
        *directory = malloc(length);
        if (*directory != NULL) {
            memcpy(*directory, explicit_directory, length);
        }
        return *directory != NULL;
    }
    DWORD count = GetEnvironmentVariableW(L"APPDATA", base, sizeof(base) / sizeof(base[0]));
    if (count == 0 || count >= sizeof(base) / sizeof(base[0])) {
        return false;
    }
    utf8 = windows_wide_to_utf8(base);
    if (utf8 == NULL) {
        return false;
    }
    length = strlen(utf8) + strlen(suffix) + 1;
    *directory = malloc(length);
    if (*directory != NULL) {
        snprintf(*directory, length, "%s%s", utf8, suffix);
    }
    free(utf8);
    return *directory != NULL;
}
