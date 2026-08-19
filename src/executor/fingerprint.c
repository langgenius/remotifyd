#include "executor/fingerprint.h"

#include <mbedtls/sha256.h>
#include <string.h>

bool fingerprint_file(FILE *file, FileFingerprint *fingerprint) {
    mbedtls_sha256_context sha256;
    uint8_t buffer[64 * 1024];
    size_t length;
    bool success = false;

    memset(fingerprint, 0, sizeof(*fingerprint));
    mbedtls_sha256_init(&sha256);
    if (fseek(file, 0, SEEK_SET) != 0 ||
        mbedtls_sha256_starts(&sha256, 0) != 0) {
        goto cleanup;
    }
    while ((length = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        if (mbedtls_sha256_update(&sha256, buffer, length) != 0 ||
            UINT64_MAX - fingerprint->size < length) {
            goto cleanup;
        }
        fingerprint->size += length;
    }
    if (ferror(file) != 0 ||
        mbedtls_sha256_finish(&sha256, fingerprint->sha256) != 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        goto cleanup;
    }
    success = true;

cleanup:
    mbedtls_sha256_free(&sha256);
    return success;
}
