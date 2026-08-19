#include "executor/fingerprint.h"
#include "fs/windows_utf8.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <windows.h>

bool fingerprint_open_regular(
    const char *path,
    FILE **file,
    FileFingerprint *fingerprint
) {
    struct _stat64 information;
    wchar_t *wide = windows_utf8_to_wide(path);
    bool success = false;

    *file = NULL;
    if (wide != NULL && _wstat64(wide, &information) == 0 &&
        (information.st_mode & _S_IFMT) == _S_IFREG) {
        *file = _wfopen(wide, L"rb");
        success = *file != NULL && fingerprint_file(*file, fingerprint);
    }
    if (!success && *file != NULL) {
        fclose(*file);
        *file = NULL;
    }
    free(wide);
    return success;
}
