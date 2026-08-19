#include "logger.h"

#include <stdarg.h>
#include <stdio.h>

static void log_message(
    const char *level,
    const char *format,
    va_list arguments
) {
    /*
     * Operational logs use stderr so stdout remains available for protocol or
     * command output. Formatting stays in this file so callers never assemble
     * a second logging convention.
     */
    fprintf(stderr, "[%s] ", level);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
}

void logf_info(const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    log_message("info", format, arguments);
    va_end(arguments);
}

void logf_error(const char *format, ...) {
    va_list arguments;

    va_start(arguments, format);
    log_message("error", format, arguments);
    va_end(arguments);
}
