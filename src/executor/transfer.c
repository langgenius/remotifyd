#include "executor/transfer.h"

#include "executor/fingerprint.h"
#include "fs/atomic_replace.h"
#include "net/http.h"

#include <curl/curl.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

enum { AUTHORIZATION_RESPONSE_LIMIT = 1024 * 1024 };

typedef struct ResponseBuffer {
    uint8_t *data;
    size_t length;
} ResponseBuffer;

typedef struct TransferAuthorization {
    char *url;
    struct curl_slist *headers;
    uint64_t size;
    uint8_t sha256[32];
} TransferAuthorization;

static size_t collect_response(
    char *data,
    size_t size,
    size_t count,
    void *context
) {
    ResponseBuffer *response = context;
    size_t length;
    uint8_t *resized;

    if (size != 0 && count > SIZE_MAX / size) {
        return 0;
    }
    length = size * count;
    if (length > AUTHORIZATION_RESPONSE_LIMIT - response->length) {
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

static bool decode_hex(const char *value, size_t length, uint8_t output[32]) {
    if (length != 64) {
        return false;
    }
    for (size_t index = 0; index < 32; index++) {
        unsigned int byte;
        char encoded[3] = {value[index * 2], value[index * 2 + 1], '\0'};
        char *end = NULL;

        errno = 0;
        byte = (unsigned int)strtoul(encoded, &end, 16);
        if (errno != 0 || end != encoded + 2) {
            return false;
        }
        output[index] = (uint8_t)byte;
    }
    return true;
}

static bool append_signed_header(
    TransferAuthorization *authorization,
    const char *name,
    const char *value
) {
    size_t length = strlen(name) + strlen(value) + 3;
    char *header = malloc(length);
    struct curl_slist *appended;

    if (header == NULL) {
        return false;
    }
    snprintf(header, length, "%s: %s", name, value);
    appended = curl_slist_append(authorization->headers, header);
    free(header);
    if (appended == NULL) {
        return false;
    }
    authorization->headers = appended;
    return true;
}

static bool parse_authorization(
    const ResponseBuffer *response,
    TransferAuthorization *authorization
) {
    yyjson_doc *document = yyjson_read(
        (const char *)response->data,
        response->length,
        YYJSON_READ_NOFLAG
    );
    yyjson_val *root;
    yyjson_val *url;
    yyjson_val *headers;
    yyjson_val *size;
    yyjson_val *digest;
    bool parsed = false;

    if (document == NULL) {
        return false;
    }
    root = yyjson_doc_get_root(document);
    url = yyjson_obj_get(root, "url");
    headers = yyjson_obj_get(root, "headers");
    size = yyjson_obj_get(root, "size_bytes");
    digest = yyjson_obj_get(root, "sha256");
    if (!yyjson_is_str(url) || yyjson_get_len(url) == 0 ||
        !yyjson_is_obj(headers) || !yyjson_is_uint(size) ||
        !yyjson_is_str(digest) ||
        !decode_hex(yyjson_get_str(digest), yyjson_get_len(digest), authorization->sha256)) {
        goto cleanup;
    }
    authorization->url = malloc(yyjson_get_len(url) + 1);
    authorization->size = yyjson_get_uint(size);
    if (authorization->url == NULL) {
        goto cleanup;
    }
    memcpy(authorization->url, yyjson_get_str(url), yyjson_get_len(url) + 1);
    size_t index;
    size_t maximum;
    yyjson_val *name;
    yyjson_val *value;
    yyjson_obj_foreach(headers, index, maximum, name, value) {
        if (!yyjson_is_str(name) || !yyjson_is_str(value)) {
            goto cleanup;
        }
        if (!append_signed_header(
                authorization,
                yyjson_get_str(name),
                yyjson_get_str(value)
            )) {
            goto cleanup;
        }
    }
    parsed = true;

cleanup:
    yyjson_doc_free(document);
    return parsed;
}

static void authorization_close(TransferAuthorization *authorization) {
    free(authorization->url);
    curl_slist_free_all(authorization->headers);
    memset(authorization, 0, sizeof(*authorization));
}

static bool authorize(
    const TransferClient *client,
    DeviceString ticket,
    const char *resource,
    TransferAuthorization *authorization
) {
    CURL *request = NULL;
    struct curl_slist *headers = NULL;
    yyjson_mut_doc *document = NULL;
    yyjson_mut_val *root;
    char *body = NULL;
    char *url = NULL;
    char *credential = NULL;
    ResponseBuffer response = {0};
    long status = 0;
    bool success = false;
    size_t length;

    document = yyjson_mut_doc_new(NULL);
    if (document == NULL) {
        goto cleanup;
    }
    root = yyjson_mut_obj(document);
    yyjson_mut_doc_set_root(document, root);
    if (!yyjson_mut_obj_add_str(document, root, "transfer_ticket", ticket.data)) {
        goto cleanup;
    }
    body = yyjson_mut_write(document, YYJSON_WRITE_NOFLAG, NULL);
    length = strlen(client->api_url) + strlen(resource) + 1;
    url = malloc(length);
    credential = malloc(strlen(client->credential) + strlen("Authorization: Bearer ") + 1);
    if (body == NULL || strlen(body) > LONG_MAX || url == NULL || credential == NULL) {
        goto cleanup;
    }
    snprintf(url, length, "%s%s", client->api_url, resource);
    sprintf(credential, "Authorization: Bearer %s", client->credential);
    struct curl_slist *appended = curl_slist_append(headers, credential);
    if (appended == NULL) {
        goto cleanup;
    }
    headers = appended;
    appended = curl_slist_append(headers, "Content-Type: application/json");
    if (appended == NULL) {
        goto cleanup;
    }
    headers = appended;
    appended = curl_slist_append(headers, "Accept: application/json");
    if (appended == NULL) {
        goto cleanup;
    }
    headers = appended;
    request = curl_easy_init();
    if (headers == NULL || request == NULL || !http_configure(request) ||
        curl_easy_setopt(request, CURLOPT_URL, url) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_HTTPHEADER, headers) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_POSTFIELDS, body) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_POSTFIELDSIZE, (long)strlen(body)) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, collect_response) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_WRITEDATA, &response) != CURLE_OK ||
        curl_easy_setopt(request, CURLOPT_TIMEOUT, 30L) != CURLE_OK ||
        curl_easy_perform(request) != CURLE_OK ||
        curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK ||
        status != 201 || !parse_authorization(&response, authorization)) {
        goto cleanup;
    }
    success = true;

cleanup:
    if (!success) {
        authorization_close(authorization);
    }
    if (request != NULL) {
        curl_easy_cleanup(request);
    }
    curl_slist_free_all(headers);
    yyjson_mut_doc_free(document);
    free(body);
    free(url);
    free(credential);
    free(response.data);
    return success;
}

static size_t discard_response(char *data, size_t size, size_t count, void *context) {
    (void)data;
    (void)context;
    if (size != 0 && count > SIZE_MAX / size) {
        return 0;
    }
    return size * count;
}

static bool perform_transfer(
    CURL *request,
    const TransferAuthorization *authorization
) {
    long status = 0;

    return http_configure(request) &&
        curl_easy_setopt(request, CURLOPT_URL, authorization->url) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_HTTPHEADER, authorization->headers) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_TIMEOUT, 600L) == CURLE_OK &&
        curl_easy_perform(request) == CURLE_OK &&
        curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK &&
        status >= 200 && status < 300;
}

bool transfer_file_stat(
    const DeviceFileStatRequestV1 *request,
    DeviceFileStatResponseV1 *response
) {
    FILE *file = NULL;
    FileFingerprint fingerprint;
    bool success = fingerprint_open_regular(request->path.data, &file, &fingerprint);

    if (success) {
        response->size_bytes = fingerprint.size;
        memcpy(response->sha256.bytes, fingerprint.sha256, sizeof(fingerprint.sha256));
    }
    if (file != NULL) {
        fclose(file);
    }
    return success;
}

bool transfer_file_upload(
    const TransferClient *client,
    const DeviceFileUploadRequestV1 *request
) {
    TransferAuthorization authorization = {0};
    FileFingerprint fingerprint;
    FILE *file = NULL;
    CURL *upload = NULL;
    bool success = false;

    if (!authorize(client, request->transfer_ticket, "/v1/file-uploads", &authorization) ||
        !fingerprint_open_regular(request->path.data, &file, &fingerprint) ||
        fingerprint.size != authorization.size ||
        memcmp(fingerprint.sha256, authorization.sha256, 32) != 0) {
        goto cleanup;
    }
    upload = curl_easy_init();
    if (upload == NULL ||
        curl_easy_setopt(upload, CURLOPT_UPLOAD, 1L) != CURLE_OK ||
        curl_easy_setopt(upload, CURLOPT_READDATA, file) != CURLE_OK ||
        curl_easy_setopt(upload, CURLOPT_WRITEFUNCTION, discard_response) != CURLE_OK ||
        curl_easy_setopt(
            upload,
            CURLOPT_INFILESIZE_LARGE,
            (curl_off_t)authorization.size
        ) != CURLE_OK || !perform_transfer(upload, &authorization)) {
        goto cleanup;
    }
    success = true;

cleanup:
    if (upload != NULL) {
        curl_easy_cleanup(upload);
    }
    if (file != NULL) {
        fclose(file);
    }
    authorization_close(&authorization);
    return success;
}

bool transfer_file_download(
    const TransferClient *client,
    const DeviceFileDownloadRequestV1 *request
) {
    TransferAuthorization authorization = {0};
    FileFingerprint fingerprint;
    AtomicReplace replacement = {0};
    FILE *file;
    CURL *download = NULL;
    bool committed = false;

    if (!authorize(client, request->transfer_ticket, "/v1/file-downloads", &authorization) ||
        !atomic_replace_prepare(request->path.data, &replacement)) {
        goto cleanup;
    }
    file = atomic_replace_file(&replacement);
    download = curl_easy_init();
    if (download == NULL ||
        curl_easy_setopt(download, CURLOPT_WRITEDATA, file) != CURLE_OK ||
        !perform_transfer(download, &authorization) ||
        fflush(file) != 0 ||
        !fingerprint_file(file, &fingerprint) ||
        fingerprint.size != authorization.size ||
        memcmp(fingerprint.sha256, authorization.sha256, 32) != 0) {
        goto cleanup;
    }
    if (!atomic_replace_commit(&replacement, request->path.data)) {
        goto cleanup;
    }
    committed = true;

cleanup:
    if (download != NULL) {
        curl_easy_cleanup(download);
    }
    atomic_replace_abort(&replacement);
    authorization_close(&authorization);
    return committed;
}
