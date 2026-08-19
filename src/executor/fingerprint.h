#ifndef REMOTIFY_EXECUTOR_FINGERPRINT_H
#define REMOTIFY_EXECUTOR_FINGERPRINT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct FileFingerprint {
    uint64_t size;
    uint8_t sha256[32];
} FileFingerprint;

bool fingerprint_open_regular(
    const char *path,
    FILE **file,
    FileFingerprint *fingerprint
);
bool fingerprint_file(FILE *file, FileFingerprint *fingerprint);

#endif
