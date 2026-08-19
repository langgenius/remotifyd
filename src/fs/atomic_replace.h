#ifndef REMOTIFY_FS_ATOMIC_REPLACE_H
#define REMOTIFY_FS_ATOMIC_REPLACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct AtomicReplace {
    char *staging_path;
    FILE *file;
    uint32_t permissions;
} AtomicReplace;

bool atomic_replace_prepare(const char *target, AtomicReplace *replacement);
FILE *atomic_replace_file(AtomicReplace *replacement);
bool atomic_replace_commit(AtomicReplace *replacement, const char *target);
void atomic_replace_abort(AtomicReplace *replacement);

#endif
