/*
 * This module is relatively independent and its function is simple, so almost
 * all of this implementation was generated directly by GPT-5.6 Sol without a
 * detailed system design. It should be reviewed and optimized later.
 */
#include "enroll.h"

#include "api_base.h"
#include "device_code.h"
#include "logger.h"
#include "state.h"

#include <ctype.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#define OPTPARSE_IMPLEMENTATION
#include <optparse.h>

extern const unsigned char remotify_ca_bundle[];
extern const size_t remotify_ca_bundle_length;

enum {
    ENROLL_OPTION_API_BASE = 256,
    ENROLL_RESPONSE_BYTES = 4096
};

/* Enrollment returns one small credential document, never an unbounded body. */
typedef struct EnrollResponse {
    char bytes[ENROLL_RESPONSE_BYTES];
    size_t length;
} EnrollResponse;

static const struct optparse_long enroll_options[] = {
    {"api-base", ENROLL_OPTION_API_BASE, OPTPARSE_REQUIRED},
    {0}
};

static size_t enroll_response_write(char *bytes, size_t size, size_t count, void *context) {
    EnrollResponse *response = context;
    size_t length = size * count;

    if (length >= sizeof(response->bytes) - response->length) return 0;

    memcpy(response->bytes + response->length, bytes, length);
    response->length += length;
    response->bytes[response->length] = '\0';
    return length;
}

static const char *enroll_platform(void) {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

static char *enroll_endpoint(const char *api_base) {
    CURLU *url = curl_url();
    char *scheme = NULL;
    char *host = NULL;
    char *endpoint = NULL;
    size_t length;
    bool slash;

    if (url == NULL ||
        strchr(api_base, '?') != NULL ||
        strchr(api_base, '#') != NULL ||
        curl_url_set(url, CURLUPART_URL, api_base, 0) != CURLUE_OK ||
        curl_url_get(url, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(url, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        (strcmp(scheme, "http") != 0 && strcmp(scheme, "https") != 0)) {
        goto cleanup;
    }

    length = strlen(api_base);
    slash = length != 0 && api_base[length - 1] == '/';
    endpoint = malloc(length + sizeof("/devices"));

    if (endpoint != NULL) sprintf(endpoint, "%s%sdevices", api_base, slash ? "" : "/");

cleanup:
    curl_free(scheme);
    curl_free(host);
    curl_url_cleanup(url);
    return endpoint;
}

static char *enroll_json_escape(const char *value) {
    static const char hexadecimal[] = "0123456789abcdef";
    const unsigned char *cursor;
    size_t length = 0;
    char *escaped;
    char *output;

    /* Only this HTTP boundary needs JSON, so it encodes strings without a library. */
    for (cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        length += *cursor == '"' || *cursor == '\\' ? 2 : *cursor < 0x20 ? 6 : 1;
    }

    escaped = malloc(length + 1);

    if (escaped == NULL) return NULL;

    output = escaped;

    for (cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        if (*cursor == '"' || *cursor == '\\') {
            *output++ = '\\';
            *output++ = (char)*cursor;
        } else if (*cursor < 0x20) {
            memcpy(output, "\\u00", 4);
            output[4] = hexadecimal[*cursor >> 4];
            output[5] = hexadecimal[*cursor & 15];
            output += 6;
        } else {
            *output++ = (char)*cursor;
        }
    }

    *output = '\0';
    return escaped;
}

static char *enroll_request_body(const char *hostname) {
    const char *device_code = device_code_get();
    const char *platform = enroll_platform();
    char *escaped_hostname = enroll_json_escape(hostname);
    char *escaped_device_code = enroll_json_escape(device_code);
    char *body = NULL;
    size_t body_length;

    if (escaped_hostname != NULL && escaped_device_code != NULL) {
        body_length = strlen(escaped_hostname) + strlen(platform) +
            strlen(escaped_device_code) +
            sizeof("{\"name\":\"\",\"os\":\"\",\"device_code\":\"\"}");
        body = malloc(body_length);
    }

    if (body != NULL) {
        sprintf(body, "{\"name\":\"%s\",\"os\":\"%s\",\"device_code\":\"%s\"}",
            escaped_hostname, platform, escaped_device_code);
    }

    free(escaped_hostname);
    free(escaped_device_code);
    return body;
}

static char *enroll_response_credential(const EnrollResponse *response) {
    static const char field[] = "\"device_credential\"";
    const char *cursor = strstr(response->bytes, field);
    const char *start;
    char *credential;
    size_t length;

    if (cursor == NULL) return NULL;

    cursor += sizeof(field) - 1;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor != ':') return NULL;
    cursor++;
    while (isspace((unsigned char)*cursor)) cursor++;
    if (*cursor != '"') return NULL;
    cursor++;
    start = cursor;

    /* Device credentials use a base64url alphabet and never need JSON escapes. */
    while (*cursor != '\0' && *cursor != '"' && *cursor != '\\') cursor++;
    if (*cursor != '"' || cursor == start) return NULL;

    length = (size_t)(cursor - start);
    credential = malloc(length + 1);

    if (credential == NULL) return NULL;

    memcpy(credential, start, length);
    credential[length] = '\0';
    return credential;
}

static bool enroll_save_api_base(const char *api_base) {
    State state = {0};
    char *stored_api_base;
    bool saved;

    if (!state_load(&state)) return false;

    stored_api_base = malloc(strlen(api_base) + 1);

    if (stored_api_base == NULL) {
        state_free(&state);
        return false;
    }

    strcpy(stored_api_base, api_base);
    free(state.api_base);
    state.api_base = stored_api_base;
    saved = state_save(&state);
    state_free(&state);
    return saved;
}

static bool enroll_save_credential(const char *api_base, const char *credential) {
    State state = {0};
    char *stored_api_base;
    char *stored_credential;
    bool saved;

    if (!state_load(&state)) return false;

    stored_api_base = malloc(strlen(api_base) + 1);
    stored_credential = malloc(strlen(credential) + 1);

    if (stored_api_base == NULL || stored_credential == NULL) {
        free(stored_api_base);
        free(stored_credential);
        state_free(&state);
        return false;
    }

    strcpy(stored_api_base, api_base);
    strcpy(stored_credential, credential);
    free(state.api_base);
    free(state.token);
    state.api_base = stored_api_base;
    state.token = stored_credential;
    saved = state_save(&state);
    state_free(&state);
    return saved;
}

static int enroll_device(const char *api_base, const char *endpoint, const char *token) {
    char hostname[UV_MAXHOSTNAMESIZE] = {0};
    size_t hostname_length = sizeof(hostname);
    char *body = NULL;
    char *authorization = NULL;
    char *credential = NULL;
    struct curl_slist *headers = NULL;
    struct curl_slist *next_header;
    CURL *request = NULL;
    CURLcode transfer;
    EnrollResponse response = {0};
    long status = 0;
    int result = EXIT_FAILURE;
    const struct curl_blob certificates = {
        .data = (void *)remotify_ca_bundle,
        .len = remotify_ca_bundle_length,
        .flags = CURL_BLOB_NOCOPY
    };

    if (uv_os_gethostname(hostname, &hostname_length) != 0) {
        logf_error("failed to read the device hostname");
        goto cleanup;
    }

    body = enroll_request_body(hostname);
    authorization = malloc(strlen(token) + sizeof("Authorization: Bearer "));
    request = curl_easy_init();

    if (body == NULL || authorization == NULL || request == NULL) {
        logf_error("failed to prepare device enrollment");
        goto cleanup;
    }

    sprintf(authorization, "Authorization: Bearer %s", token);
    headers = curl_slist_append(NULL, authorization);

    if (headers == NULL) goto prepare_failed;

    next_header = curl_slist_append(headers, "Content-Type: application/json");
    if (next_header == NULL) goto prepare_failed;
    headers = next_header;

    next_header = curl_slist_append(headers, "Accept: application/json");
    if (next_header == NULL) goto prepare_failed;
    headers = next_header;

    /* The embedded roots keep HTTPS independent from an old Windows store. */
    if (curl_easy_setopt(request, CURLOPT_URL, endpoint) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_HTTPHEADER, headers) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_POSTFIELDS, body) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_POSTFIELDSIZE, (long)strlen(body)) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, enroll_response_write) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_WRITEDATA, &response) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_CAINFO, NULL) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_CAINFO_BLOB, &certificates) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_TIMEOUT, 30L) != CURLE_OK) {
        goto prepare_failed;
    }

    transfer = curl_easy_perform(request);

    if (transfer != CURLE_OK) {
        logf_error("device enrollment request failed: %s", curl_easy_strerror(transfer));
        goto cleanup;
    }

    if (curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK) {
        logf_error("failed to read the device enrollment response");
        goto cleanup;
    }

    if (status != 201) {
        logf_error("device enrollment was rejected: HTTP %ld", status);
        goto cleanup;
    }

    credential = enroll_response_credential(&response);

    if (credential == NULL) {
        logf_error("device enrollment response is incomplete");
        goto cleanup;
    }

    if (!enroll_save_credential(api_base, credential)) {
        logf_error("failed to save the device credential");
        goto cleanup;
    }

    logf_info("device enrolled: %s", device_code_get());
    logf_info("successfully connected to remotify");
    result = EXIT_SUCCESS;
    goto cleanup;

prepare_failed:
    logf_error("failed to prepare device enrollment");

cleanup:
    if (request != NULL) curl_easy_cleanup(request);
    curl_slist_free_all(headers);
    free(body);
    free(authorization);
    free(credential);
    return result;
}

int process_enroll(int argc, char **argv) {
    struct optparse parser;
    const char *selected_api_base = get_api_base();
    const char *parsed_api_base = NULL;
    const char *token;
    char *endpoint;
    int option;
    int result;

    (void)argc;

    /* argv[1] becomes the parser's program name, isolating this subcommand. */
    optparse_init(&parser, argv + 1);

    while ((option = optparse_long(&parser, enroll_options, NULL)) != -1) {
        switch (option) {
        case ENROLL_OPTION_API_BASE:
            parsed_api_base = parser.optarg;
            break;

        case '?':
            logf_error("%s", parser.errmsg);
            return EXIT_FAILURE;
        }
    }

    token = optparse_arg(&parser);

    if (token == NULL || optparse_arg(&parser) != NULL) {
        logf_error("usage: %s enroll [--api-base URL] TOKEN", argv[0]);
        return EXIT_FAILURE;
    }

    if (parsed_api_base != NULL) selected_api_base = parsed_api_base;
    endpoint = enroll_endpoint(selected_api_base);

    if (endpoint == NULL) {
        logf_error("invalid API base: %s", selected_api_base);
        return EXIT_FAILURE;
    }

    /* An explicit API base is local configuration, independent of token redemption. */
    if (parsed_api_base != NULL && !enroll_save_api_base(parsed_api_base)) {
        logf_error("failed to save API base");
        free(endpoint);
        return EXIT_FAILURE;
    }

    result = enroll_device(selected_api_base, endpoint, token);
    free(endpoint);
    return result;
}
