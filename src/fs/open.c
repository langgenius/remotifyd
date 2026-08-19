#include "fs/open.h"

#if defined(_WIN32)
#include "fs/windows_utf8.h"

#include <stdlib.h>
#include <wchar.h>

FILE *fs_open(const char *path, const char *mode) {
    wchar_t *wide_path = windows_utf8_to_wide(path);
    wchar_t *wide_mode = windows_utf8_to_wide(mode);
    FILE *file = wide_path != NULL && wide_mode != NULL
        ? _wfopen(wide_path, wide_mode)
        : NULL;

    free(wide_path);
    free(wide_mode);
    return file;
}
#else
FILE *fs_open(const char *path, const char *mode) {
    return fopen(path, mode);
}
#endif
