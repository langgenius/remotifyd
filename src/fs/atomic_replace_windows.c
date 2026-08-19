#include "fs/atomic_replace.h"
#include "fs/windows_utf8.h"

#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static bool make_directories(wchar_t *path) {
    wchar_t *cursor = path;

    if (path[0] != L'\0' && path[1] == L':') {
        cursor = path + 2;
    }
    for (; *cursor != L'\0'; cursor++) {
        wchar_t separator;

        if (*cursor != L'\\' && *cursor != L'/') {
            continue;
        }
        separator = *cursor;
        *cursor = L'\0';
        if (path[0] != L'\0' && !CreateDirectoryW(path, NULL) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            *cursor = separator;
            return false;
        }
        *cursor = separator;
    }
    return true;
}

bool atomic_replace_prepare(const char *target, AtomicReplace *replacement) {
    wchar_t *wide = windows_utf8_to_wide(target);
    wchar_t *separator;
    wchar_t *forward_separator;
    wchar_t directory[MAX_PATH];
    wchar_t staging[MAX_PATH];
    bool success = false;

    memset(replacement, 0, sizeof(*replacement));
    if (wide == NULL || wcslen(wide) >= MAX_PATH) {
        goto cleanup;
    }
    wcscpy(directory, wide);
    separator = wcsrchr(directory, L'\\');
    forward_separator = wcsrchr(directory, L'/');
    if (separator == NULL ||
        (forward_separator != NULL && forward_separator > separator)) {
        separator = forward_separator;
    }
    if (separator == NULL) {
        wcscpy(directory, L".");
    } else {
        *separator = L'\0';
        if (!make_directories(directory)) {
            goto cleanup;
        }
    }
    if (GetTempFileNameW(directory, L"rmt", 0, staging) == 0) {
        goto cleanup;
    }
    replacement->staging_path = windows_wide_to_utf8(staging);
    replacement->file = _wfopen(staging, L"w+b");
    if (replacement->staging_path == NULL || replacement->file == NULL) {
        DeleteFileW(staging);
        goto cleanup;
    }
    success = true;

cleanup:
    free(wide);
    if (!success) {
        atomic_replace_abort(replacement);
    }
    return success;
}

FILE *atomic_replace_file(AtomicReplace *replacement) {
    return replacement->file;
}

bool atomic_replace_commit(AtomicReplace *replacement, const char *target) {
    wchar_t *staging = windows_utf8_to_wide(replacement->staging_path);
    wchar_t *wide_target = windows_utf8_to_wide(target);
    bool success = replacement->file != NULL && staging != NULL && wide_target != NULL &&
        fflush(replacement->file) == 0 &&
        _commit(_fileno(replacement->file)) == 0 &&
        fclose(replacement->file) == 0;

    replacement->file = NULL;
    if (success) {
        success = MoveFileExW(
            staging,
            wide_target,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        ) != 0;
    }
    if (success) {
        free(replacement->staging_path);
        replacement->staging_path = NULL;
    }
    free(staging);
    free(wide_target);
    return success;
}

void atomic_replace_abort(AtomicReplace *replacement) {
    wchar_t *staging = replacement->staging_path == NULL
        ? NULL
        : windows_utf8_to_wide(replacement->staging_path);

    if (replacement->file != NULL) {
        fclose(replacement->file);
    }
    if (staging != NULL) {
        DeleteFileW(staging);
    }
    free(staging);
    free(replacement->staging_path);
    memset(replacement, 0, sizeof(*replacement));
}
