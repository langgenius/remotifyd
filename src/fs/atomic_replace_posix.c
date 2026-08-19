#include "fs/atomic_replace.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool make_directories(char *path) {
    for (char *cursor = path + 1; *cursor != '\0'; cursor++) {
        if (*cursor != '/') {
            continue;
        }
        *cursor = '\0';
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            *cursor = '/';
            return false;
        }
        *cursor = '/';
    }
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool atomic_replace_prepare(
    const char *target,
    AtomicReplace *replacement
) {
    struct stat information;
    char *copy = NULL;
    char *slash;
    const char *directory = ".";
    size_t length;

    int descriptor;

    memset(replacement, 0, sizeof(*replacement));
    replacement->permissions = 0600;
    if (target[0] == '\0') {
        return false;
    }
    if (lstat(target, &information) == 0) {
        if (!S_ISREG(information.st_mode)) {
            return false;
        }
        replacement->permissions = (uint32_t)(information.st_mode & 0777);
    } else if (errno != ENOENT) {
        return false;
    }
    copy = strdup(target);
    if (copy == NULL) {
        return false;
    }
    slash = strrchr(copy, '/');
    if (slash != NULL) {
        *slash = '\0';
        directory = copy[0] == '\0' ? "/" : copy;
        if (!make_directories(copy)) {
            free(copy);
            return false;
        }
    }
    length = strlen(directory) + strlen("/.remotify-staging-XXXXXX") + 1;
    replacement->staging_path = malloc(length);
    if (replacement->staging_path == NULL) {
        free(copy);
        return false;
    }
    snprintf(
        replacement->staging_path,
        length,
        "%s/.remotify-staging-XXXXXX",
        directory
    );
    descriptor = mkstemp(replacement->staging_path);
    free(copy);
    if (descriptor < 0) {
        atomic_replace_abort(replacement);
        return false;
    }
    replacement->file = fdopen(descriptor, "w+b");
    if (replacement->file == NULL) {
        close(descriptor);
        atomic_replace_abort(replacement);
        return false;
    }
    return true;
}

FILE *atomic_replace_file(AtomicReplace *replacement) {
    return replacement->file;
}

bool atomic_replace_commit(AtomicReplace *replacement, const char *target) {
    bool success = replacement->file != NULL &&
        fflush(replacement->file) == 0 &&
        fsync(fileno(replacement->file)) == 0 &&
        fclose(replacement->file) == 0;

    replacement->file = NULL;
    if (success) {
        success = chmod(
            replacement->staging_path,
            (mode_t)replacement->permissions
        ) == 0 && rename(replacement->staging_path, target) == 0;
    }
    if (success) {
        free(replacement->staging_path);
        replacement->staging_path = NULL;
    }
    return success;
}

void atomic_replace_abort(AtomicReplace *replacement) {
    if (replacement->file != NULL) {
        fclose(replacement->file);
    }
    if (replacement->staging_path != NULL) {
        unlink(replacement->staging_path);
    }
    free(replacement->staging_path);
    memset(replacement, 0, sizeof(*replacement));
}
