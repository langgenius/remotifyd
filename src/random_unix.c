#include "random.h"

#if !defined(_WIN32)

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>

bool random_bytes(void *bytes, unsigned int length) {
    unsigned char *cursor = bytes;
    size_t remaining = length;
    int descriptor;

    /* /dev/urandom is the common native CSPRNG boundary on supported Unix systems. */
    descriptor = open("/dev/urandom", O_RDONLY);

    if (descriptor < 0) return false;

    while (remaining != 0) {
        ssize_t received = read(descriptor, cursor, remaining);

        if (received < 0 && errno == EINTR) continue;

        if (received <= 0) {
            close(descriptor);
            return false;
        }

        cursor += received;
        remaining -= (size_t)received;
    }

    close(descriptor);
    return true;
}

#endif
