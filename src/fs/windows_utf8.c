#include "fs/windows_utf8.h"

#include <stdlib.h>
#include <windows.h>

wchar_t *windows_utf8_to_wide(const char *value) {
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, NULL, 0);
    wchar_t *wide = length > 0 ? malloc((size_t)length * sizeof(*wide)) : NULL;

    if (wide == NULL || MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value,
            -1,
            wide,
            length
        ) == 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

char *windows_wide_to_utf8(const wchar_t *value) {
    int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
    char *utf8 = length > 0 ? malloc((size_t)length) : NULL;

    if (utf8 == NULL || WideCharToMultiByte(
            CP_UTF8,
            0,
            value,
            -1,
            utf8,
            length,
            NULL,
            NULL
        ) == 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}
