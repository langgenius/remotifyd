#include "net/http.h"

#include <stddef.h>

extern const unsigned char remotify_ca_bundle[];
extern const size_t remotify_ca_bundle_length;

bool http_configure(CURL *request) {
    const struct curl_blob certificates = {
        .data = (void *)remotify_ca_bundle,
        .len = remotify_ca_bundle_length,
        .flags = CURL_BLOB_NOCOPY
    };

    /*
     * The daemon ships one trust store so HTTPS behaves identically on old
     * Windows installations and current systems. The blob is immutable and
     * lives for the process lifetime, which is exactly CURL_BLOB_NOCOPY's
     * ownership contract.
     */
    return request != NULL &&
        curl_easy_setopt(request, CURLOPT_CAINFO, NULL) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_CAINFO_BLOB, &certificates) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L) == CURLE_OK;
}
