#include "install/enroll.h"

#include "install/identity.h"
#include "install/state.h"
#include "logger.h"
#include "net/http.h"

#include <curl/curl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

enum { ENROLLMENT_RESPONSE_LIMIT = 64 * 1024 };

typedef struct EnrollmentResponse {
    char *data;
    size_t length;
} EnrollmentResponse;

static size_t collect_response(char *data, size_t size, size_t count, void *context) {
    EnrollmentResponse *response = context;
    size_t length;
    char *resized;

    if (size != 0 && count > SIZE_MAX / size) {
        return 0;
    }
    length = size * count;
    if (length > ENROLLMENT_RESPONSE_LIMIT - response->length) {
        return 0;
    }
    resized = realloc(response->data, response->length + length + 1);
    if (resized == NULL) {
        return 0;
    }
    response->data = resized;
    memcpy(response->data + response->length, data, length);
    response->length += length;
    response->data[response->length] = '\0';
    return length;
}

static char *device_endpoint(const char *api_url) {
    CURLU *url = curl_url();
    char *scheme = NULL;
    char *host = NULL;
    char *path = NULL;
    char *endpoint = NULL;

    if (url == NULL || curl_url_set(url, CURLUPART_URL, api_url, 0) != CURLUE_OK ||
        curl_url_get(url, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(url, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        (strcmp(scheme, "http") != 0 && strcmp(scheme, "https") != 0) ||
        (curl_url_get(url, CURLUPART_PATH, &path, 0) == CURLUE_OK &&
            strcmp(path, "/") != 0) ||
        curl_url_set(url, CURLUPART_PATH, "/v1/devices", 0) != CURLUE_OK ||
        curl_url_get(url, CURLUPART_URL, &endpoint, 0) != CURLUE_OK) {
        curl_free(endpoint);
        endpoint = NULL;
    }
    curl_free(scheme);
    curl_free(host);
    curl_free(path);
    curl_url_cleanup(url);
    return endpoint;
}

static char *request_body(
    const char *name,
    const char *device_code
) {
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = document == NULL ? NULL : yyjson_mut_obj(document);
    char *body = NULL;

    if (root != NULL) {
        yyjson_mut_doc_set_root(document, root);
        if (yyjson_mut_obj_add_str(document, root, "name", name) &&
            yyjson_mut_obj_add_str(document, root, "os", identity_platform()) &&
            yyjson_mut_obj_add_str(document, root, "device_code", device_code)) {
            body = yyjson_mut_write(document, YYJSON_WRITE_NOFLAG, NULL);
        }
    }
    yyjson_mut_doc_free(document);
    return body;
}

static char *response_credential(const EnrollmentResponse *response) {
    yyjson_doc *document = yyjson_read(
        response->data,
        response->length,
        YYJSON_READ_NOFLAG
    );
    yyjson_val *credential;
    char *copy = NULL;

    if (document == NULL) {
        return NULL;
    }
    credential = yyjson_obj_get(yyjson_doc_get_root(document), "device_credential");
    if (yyjson_is_str(credential) && yyjson_get_len(credential) != 0) {
        copy = strdup(yyjson_get_str(credential));
    }
    yyjson_doc_free(document);
    return copy;
}

int enroll_device(
    const char *api_url,
    const char *token,
    const char *name,
    const char *state_directory
) {
    char *directory = NULL;
    char *device_code = NULL;
    char *hostname = NULL;
    char *endpoint = NULL;
    char *body = NULL;
    char *authorization = NULL;
    char *credential = NULL;
    struct curl_slist *headers = NULL;
    CURL *request = NULL;
    EnrollmentResponse response = {0};
    long status = 0;
    int result = 1;

    if (!state_resolve_directory(state_directory, &directory) ||
        !identity_load_or_create(directory, &device_code)) {
        logf_error("failed to load or create device identity");
        goto cleanup;
    }
    if (name == NULL && !identity_hostname(&hostname)) {
        logf_error("failed to resolve device name");
        goto cleanup;
    }
    endpoint = device_endpoint(api_url);
    body = request_body(name == NULL ? hostname : name, device_code);
    authorization = malloc(strlen(token) + strlen("Authorization: Bearer ") + 1);
    request = curl_easy_init();
    if (endpoint == NULL || body == NULL || authorization == NULL || request == NULL ||
        strlen(body) > LONG_MAX) {
        logf_error("failed to prepare enrollment request");
        goto cleanup;
    }
    sprintf(authorization, "Authorization: Bearer %s", token);
    headers = curl_slist_append(headers, authorization);
    headers = headers == NULL ? NULL : curl_slist_append(headers, "Content-Type: application/json");
    headers = headers == NULL ? NULL : curl_slist_append(headers, "Accept: application/json");
    if (headers == NULL || !http_configure(request) ||
        curl_easy_setopt(request, CURLOPT_URL, endpoint) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_HTTPHEADER, headers) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_POSTFIELDS, body) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_POSTFIELDSIZE, (long)strlen(body)) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, collect_response) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_WRITEDATA, &response) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_TIMEOUT, 30L) != CURLE_OK ||
        curl_easy_perform(request) != CURLE_OK ||
        curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK) {
        logf_error("device enrollment request failed");
        goto cleanup;
    }
    if (status != 201 || (credential = response_credential(&response)) == NULL) {
        logf_error(
            "device enrollment was rejected: HTTP %ld",
            status
        );
        goto cleanup;
    }
    if (!state_save(directory, api_url, credential)) {
        logf_error("failed to persist enrolled device");
        goto cleanup;
    }
    printf("{\"device_code\":\"%s\"}\n", device_code);
    result = 0;

cleanup:
    if (request != NULL) {
        curl_easy_cleanup(request);
    }
    curl_slist_free_all(headers);
    curl_free(endpoint);
    free(directory);
    free(device_code);
    free(hostname);
    free(body);
    free(authorization);
    free(credential);
    free(response.data);
    return result;
}
