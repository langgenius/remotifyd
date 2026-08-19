#include "executor/fingerprint.h"

#include <sys/stat.h>

bool fingerprint_open_regular(
    const char *path,
    FILE **file,
    FileFingerprint *fingerprint
) {
    struct stat information;

    *file = NULL;
    if (lstat(path, &information) != 0 || !S_ISREG(information.st_mode)) {
        return false;
    }
    *file = fopen(path, "rb");
    if (*file == NULL || !fingerprint_file(*file, fingerprint)) {
        if (*file != NULL) {
            fclose(*file);
            *file = NULL;
        }
        return false;
    }
    return true;
}
