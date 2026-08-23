#include "random.h"

#if defined(_WIN32) && _WIN32_WINNT >= 0x0600

#include <windows.h>
#include <bcrypt.h>

bool random_bytes(void *bytes, unsigned int length) {
    BCRYPT_ALG_HANDLE provider;
    NTSTATUS status;

    /* Vista and later expose their native CSPRNG through CNG. */
    status = BCryptOpenAlgorithmProvider(&provider, BCRYPT_RNG_ALGORITHM, NULL, 0);

    if (status < 0) return false;

    status = BCryptGenRandom(provider, bytes, length, 0);
    BCryptCloseAlgorithmProvider(provider, 0);
    return status >= 0;
}

#endif
