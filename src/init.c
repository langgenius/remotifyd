#include "init.h"

#include "logger.h"

#include <curl/curl.h>

bool process_init(void) {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        logf_error("failed to initialize libcurl");
        return false;
    }

    return true;
}

void process_cleanup(void) {
    /* Every role has finished using libcurl before control returns to main. */
    curl_global_cleanup();
}
