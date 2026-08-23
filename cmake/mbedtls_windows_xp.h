#ifndef REMOTIFY_MBEDTLS_WINDOWS_XP_H
#define REMOTIFY_MBEDTLS_WINDOWS_XP_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/*
 * Windows XP's msvcrt.dll does not export vsnprintf_s. Mbed TLS only uses
 * this call with _TRUNCATE, so keep the same truncation behavior while using
 * MinGW's statically linked vsnprintf implementation.
 */
static inline int remotify_mbedtls_xp_vsnprintf_s(
    char *buffer,
    size_t buffer_size,
    size_t count,
    const char *format,
    va_list arguments
) {
    int written;

    (void)count;

    written = vsnprintf(buffer, buffer_size, format, arguments);
    if (written < 0 || (size_t)written >= buffer_size) {
        buffer[buffer_size - 1] = '\0';
        return -1;
    }

    return written;
}

#define vsnprintf_s remotify_mbedtls_xp_vsnprintf_s

#endif
