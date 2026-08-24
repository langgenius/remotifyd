#include "executor_worker.h"

#include "ipc_response_types.h"
#include "logger.h"

#include <curl/curl.h>
#include <fcntl.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern const unsigned char remotify_ca_bundle[];
extern const size_t remotify_ca_bundle_length;

enum {
    FILE_TRANSFER_CONNECT_TIMEOUT_SECONDS = 30,
    FILE_TRANSFER_TIMEOUT_SECONDS = 8 * 60,
    FILE_STAT_READ_BYTES = 16 * 1024
};

#if MBEDTLS_VERSION_MAJOR < 3
#define FILE_STAT_SHA256_STARTS mbedtls_sha256_starts_ret
#define FILE_STAT_SHA256_UPDATE mbedtls_sha256_update_ret
#define FILE_STAT_SHA256_FINISH mbedtls_sha256_finish_ret
#else
#define FILE_STAT_SHA256_STARTS mbedtls_sha256_starts
#define FILE_STAT_SHA256_UPDATE mbedtls_sha256_update
#define FILE_STAT_SHA256_FINISH mbedtls_sha256_finish
#endif

typedef struct FileTransferIO {
    uv_loop_t *loop;
    uv_file file;
} FileTransferIO;

static size_t file_transfer_discard(char *bytes, size_t size, size_t count, void *data) {
    (void)bytes;
    (void)data;
    if (size != 0 && count > SIZE_MAX / size) return 0;
    return size * count;
}

static size_t file_transfer_read(char *bytes, size_t size, size_t count, void *data) {
    FileTransferIO *io = data;
    uv_fs_t request;
    size_t capacity;
    uv_buf_t buffer;
    int result;

    if (size != 0 && count > SIZE_MAX / size) return CURL_READFUNC_ABORT;
    capacity = size * count;
    if (capacity > UINT_MAX) capacity = UINT_MAX;
    buffer = uv_buf_init(bytes, (unsigned int)capacity);
    result = uv_fs_read(io->loop, &request, io->file, &buffer, 1, -1, NULL);
    uv_fs_req_cleanup(&request);
    if (result < 0) {
        return CURL_READFUNC_ABORT;
    }
    return (size_t)result;
}

static size_t file_transfer_write(char *bytes, size_t size, size_t count, void *data) {
    FileTransferIO *io = data;
    size_t length;
    size_t written = 0;

    if (size != 0 && count > SIZE_MAX / size) return 0;
    length = size * count;
    while (written < length) {
        uv_fs_t request;
        size_t remaining = length - written;
        unsigned int chunk = remaining > UINT_MAX ? UINT_MAX : (unsigned int)remaining;
        uv_buf_t buffer = uv_buf_init(bytes + written, chunk);
        int result = uv_fs_write(io->loop, &request, io->file, &buffer, 1, -1, NULL);

        uv_fs_req_cleanup(&request);
        if (result <= 0) {
            return 0;
        }
        written += (size_t)result;
    }
    return written;
}

static bool file_transfer_prepare_curl(CURL *curl, const char *url) {
    const struct curl_blob certificates = {
        .data = (void *)remotify_ca_bundle,
        .len = remotify_ca_bundle_length,
        .flags = CURL_BLOB_NOCOPY
    };

    return curl_easy_setopt(curl, CURLOPT_URL, url) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)FILE_TRANSFER_CONNECT_TIMEOUT_SECONDS) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)FILE_TRANSFER_TIMEOUT_SECONDS) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_CAINFO, NULL) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &certificates) == CURLE_OK;
}

static bool file_transfer_http_succeeded(CURL *curl, CURLcode result) {
    long status = 0;
    return result == CURLE_OK &&
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK &&
        status >= 200 && status < 300;
}

static bool file_transfer_close(uv_loop_t *loop, uv_file file) {
    uv_fs_t request;
    int result = uv_fs_close(loop, &request, file, NULL);

    uv_fs_req_cleanup(&request);
    return result == 0;
}

static IpcStatus file_stat_compute(
    uv_loop_t *loop,
    const char *path,
    IpcFileStatResponse *response
) {
    mbedtls_sha256_context sha256;
    uv_file file = -1;
    uint8_t bytes[FILE_STAT_READ_BYTES];
    uint64_t size_bytes;
    uv_fs_t request;
    IpcStatus status = IPC_STATUS_FAILED;

    mbedtls_sha256_init(&sha256);
    file = uv_fs_open(loop, &request, path, O_RDONLY, 0, NULL);
    uv_fs_req_cleanup(&request);
    if (file == UV_ENOENT) {
        status = IPC_STATUS_NOT_FOUND;
        goto finished;
    }
    if (file < 0) goto finished;
    if (uv_fs_fstat(loop, &request, file, NULL) != 0) {
        uv_fs_req_cleanup(&request);
        goto finished;
    }
    if (!S_ISREG(request.statbuf.st_mode)) {
        uv_fs_req_cleanup(&request);
        goto finished;
    }
    size_bytes = (uint64_t)request.statbuf.st_size;
    uv_fs_req_cleanup(&request);
    if (FILE_STAT_SHA256_STARTS(&sha256, 0) != 0) goto finished;

    for (;;) {
        uv_buf_t buffer = uv_buf_init((char *)bytes, FILE_STAT_READ_BYTES);
        int result = uv_fs_read(loop, &request, file, &buffer, 1, -1, NULL);

        uv_fs_req_cleanup(&request);
        if (result < 0) goto finished;
        if (result == 0) break;
        if (FILE_STAT_SHA256_UPDATE(&sha256, bytes, (size_t)result) != 0) goto finished;
    }

    if (FILE_STAT_SHA256_FINISH(&sha256, response->sha256) != 0) goto finished;
    response->size_bytes = size_bytes;
    status = IPC_STATUS_OK;

finished:
    if (file >= 0 && !file_transfer_close(loop, file)) status = IPC_STATUS_FAILED;
    mbedtls_sha256_free(&sha256);
    return status;
}

static bool file_transfer_upload(uv_loop_t *loop, const char *path, const char *url) {
    FileTransferIO io = {.loop = loop, .file = -1};
    uv_fs_t request;
    CURL *curl = NULL;
    curl_off_t length;
    bool succeeded = false;

    io.file = uv_fs_open(loop, &request, path, O_RDONLY, 0, NULL);
    uv_fs_req_cleanup(&request);
    if (io.file < 0) goto finished;
    if (uv_fs_fstat(loop, &request, io.file, NULL) != 0) {
        uv_fs_req_cleanup(&request);
        goto finished;
    }
    if (!S_ISREG(request.statbuf.st_mode)) {
        uv_fs_req_cleanup(&request);
        goto finished;
    }
    length = (curl_off_t)request.statbuf.st_size;
    uv_fs_req_cleanup(&request);
    curl = curl_easy_init();
    if (curl == NULL || !file_transfer_prepare_curl(curl, url) ||
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, file_transfer_read) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_READDATA, &io) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, length) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_transfer_discard) != CURLE_OK) {
        goto finished;
    }
    succeeded = file_transfer_http_succeeded(curl, curl_easy_perform(curl));

finished:
    if (curl != NULL) curl_easy_cleanup(curl);
    if (io.file >= 0 && !file_transfer_close(loop, io.file)) succeeded = false;
    return succeeded;
}

static char *file_transfer_temporary_path(const char *target, uint32_t request_id) {
    size_t target_length = strlen(target);
    size_t suffix_length = sizeof(".remotify-download-00000000") - 1;
    char *result;

    if (target_length > SIZE_MAX - suffix_length - 1) return NULL;
    result = malloc(target_length + suffix_length + 1);
    if (result == NULL) return NULL;
    sprintf(result, "%s.remotify-download-%08x", target, (unsigned int)request_id);
    return result;
}

static bool file_transfer_download(
    uv_loop_t *loop,
    uint32_t request_id,
    const char *target,
    const char *url
) {
    FileTransferIO io = {.loop = loop, .file = -1};
    char *temporary = file_transfer_temporary_path(target, request_id);
    uv_fs_t request;
    CURL *curl = NULL;
    int fs_result;
    int mode = 0600;
    bool temporary_exists = false;
    bool succeeded = false;

    if (temporary == NULL) return false;
    fs_result = uv_fs_lstat(loop, &request, target, NULL);
    if (fs_result == 0) {
        if (!S_ISREG(request.statbuf.st_mode)) {
            uv_fs_req_cleanup(&request);
            goto finished;
        }
        mode = request.statbuf.st_mode & 0777;
    } else if (fs_result != UV_ENOENT) {
        uv_fs_req_cleanup(&request);
        goto finished;
    }
    uv_fs_req_cleanup(&request);
    io.file = uv_fs_open(loop, &request, temporary, O_CREAT | O_EXCL | O_WRONLY, 0600, NULL);
    uv_fs_req_cleanup(&request);
    if (io.file < 0) goto finished;
    temporary_exists = true;
    curl = curl_easy_init();
    if (curl == NULL || !file_transfer_prepare_curl(curl, url) ||
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_transfer_write) != CURLE_OK ||
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &io) != CURLE_OK) {
        goto finished;
    }
    if (!file_transfer_http_succeeded(curl, curl_easy_perform(curl))) goto finished;
    if (uv_fs_fsync(loop, &request, io.file, NULL) != 0) {
        uv_fs_req_cleanup(&request);
        goto finished;
    }
    uv_fs_req_cleanup(&request);
    {
        uv_file file = io.file;
        io.file = -1;
        if (!file_transfer_close(loop, file)) goto finished;
    }
    if (uv_fs_chmod(loop, &request, temporary, mode, NULL) != 0) {
        uv_fs_req_cleanup(&request);
        goto finished;
    }
    uv_fs_req_cleanup(&request);
    if (uv_fs_rename(loop, &request, temporary, target, NULL) != 0) {
        uv_fs_req_cleanup(&request);
        goto finished;
    }
    uv_fs_req_cleanup(&request);
    temporary_exists = false;
    succeeded = true;

finished:
    if (curl != NULL) curl_easy_cleanup(curl);
    if (io.file >= 0) {
        uv_file file = io.file;
        io.file = -1;
        if (!file_transfer_close(loop, file)) succeeded = false;
    }
    if (temporary_exists) {
        int result = uv_fs_unlink(loop, &request, temporary, NULL);
        uv_fs_req_cleanup(&request);
        if (result != 0) logf_error("failed to remove incomplete file download");
    }
    free(temporary);
    return succeeded;
}

void executor_worker_handle_file_transfer_request(
    ExecutorWorker *worker,
    const IpcHeader *header,
    const IpcFileTransferRequestHeader *request
) {
    size_t remaining;
    const uint8_t *path_bytes;
    const uint8_t *url_bytes;
    char *arguments = NULL;
    char *path;
    char *url;
    bool succeeded = false;
    IpcHeader response = {
        .request_id = header->request_id,
        .type = header->type,
        .response_type = IPC_SINGLE,
        .status = IPC_STATUS_FAILED
    };

    if (header->body_length < sizeof(*request)) goto finished;
    remaining = header->body_length - sizeof(*request);
    if (request->path_length == 0 || request->url_length == 0 ||
        request->path_length > remaining ||
        request->url_length != remaining - request->path_length) goto finished;
    path_bytes = (const uint8_t *)(request + 1);
    url_bytes = path_bytes + request->path_length;
    if (memchr(path_bytes, 0, request->path_length) != NULL ||
        memchr(url_bytes, 0, request->url_length) != NULL) goto finished;
    arguments = malloc(
        (size_t)request->path_length + 1 + (size_t)request->url_length + 1
    );
    if (arguments == NULL) goto finished;
    path = arguments;
    url = arguments + request->path_length + 1;
    memcpy(path, path_bytes, request->path_length);
    path[request->path_length] = '\0';
    memcpy(url, url_bytes, request->url_length);
    url[request->url_length] = '\0';
    succeeded = header->type == IPC_REQUEST_FILE_UPLOAD
        ? file_transfer_upload(&worker->loop, path, url)
        : file_transfer_download(&worker->loop, (uint32_t)header->request_id, path, url);

finished:
    free(arguments);
    if (succeeded) response.status = IPC_STATUS_OK;
    executor_worker_output(worker, &response, NULL);
}

void executor_worker_handle_file_stat_request(
    ExecutorWorker *worker,
    const IpcHeader *header,
    const IpcFileStatRequestHeader *request
) {
    IpcFileStatResponse stat = {0};
    IpcHeader response = {
        .request_id = header->request_id,
        .type = IPC_REQUEST_FILE_STAT,
        .response_type = IPC_SINGLE,
        .status = IPC_STATUS_FAILED
    };
    char *path = NULL;

    if (header->body_length < sizeof(*request)) goto finished;
    if (request->path_length == 0 ||
        request->path_length != header->body_length - sizeof(*request)) goto finished;
    if (memchr(request + 1, 0, request->path_length) != NULL) goto finished;
    path = malloc((size_t)request->path_length + 1);
    if (path == NULL) goto finished;
    memcpy(path, request + 1, request->path_length);
    path[request->path_length] = '\0';
    response.status = file_stat_compute(&worker->loop, path, &stat);
    if (response.status != IPC_STATUS_OK) goto finished;
    response.body_length = sizeof(stat);

finished:
    free(path);
    executor_worker_output(worker, &response, response.body_length == 0 ? NULL : (const uint8_t *)&stat);
}
