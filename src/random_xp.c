#include "random.h"

#if defined(_WIN32) && _WIN32_WINNT < 0x0600

#include <windows.h>
#include <wincrypt.h>

bool random_bytes(void *bytes, unsigned int length) {
    HCRYPTPROV provider;
    bool succeed;

    /* Windows XP has no CNG, so its native CryptoAPI provider owns the CSPRNG. */
    if (!CryptAcquireContextW(
            &provider,
            NULL,
            NULL,
            PROV_RSA_FULL,
            CRYPT_VERIFYCONTEXT | CRYPT_SILENT
        )) {
        return false;
    }

    succeed = CryptGenRandom(provider, (DWORD)length, bytes) != FALSE;
    CryptReleaseContext(provider, 0);
    return succeed;
}

#endif
