#include "executor_worker_patch_path.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

static char *patch_cwd_string(const uint8_t *cwd, size_t cwd_length) {
    char *value;

    if (cwd_length == 0) {
        value = malloc(2);
        if (value != NULL) memcpy(value, ".", 2);
        return value;
    }
    if (cwd_length == SIZE_MAX || memchr(cwd, 0, cwd_length) != NULL) return NULL;
    value = malloc(cwd_length + 1);
    if (value == NULL) return NULL;
    memcpy(value, cwd, cwd_length);
    value[cwd_length] = 0;
    return value;
}

#ifndef _WIN32
static char *patch_root_absolute(uv_loop_t *loop, const char *cwd) {
    uv_fs_t request;
    char *absolute = NULL;
    int result = uv_fs_realpath(loop, &request, cwd, NULL);

    if (result == 0) {
        const char *resolved = request.ptr;
        size_t length = strlen(resolved);

        if (length != SIZE_MAX) {
            absolute = malloc(length + 1);
            if (absolute != NULL) memcpy(absolute, resolved, length + 1);
        }
    }
    uv_fs_req_cleanup(&request);
    return absolute;
}
#else
static wchar_t *patch_utf8_to_wide(const char *value) {
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, NULL, 0);
    wchar_t *wide;

    if (length <= 0 || (size_t)length > SIZE_MAX / sizeof(*wide)) return NULL;
    wide = malloc((size_t)length * sizeof(*wide));
    if (wide == NULL) return NULL;
    if (MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, wide, length
    ) == 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static char *patch_wide_to_utf8(const wchar_t *value) {
    int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
    char *utf8;

    if (length <= 0) return NULL;
    utf8 = malloc((size_t)length);
    if (utf8 == NULL) return NULL;
    if (WideCharToMultiByte(
        CP_UTF8, 0, value, -1, utf8, length, NULL, NULL
    ) == 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static char *patch_root_absolute(uv_loop_t *loop, const char *cwd) {
    wchar_t *wide_cwd = patch_utf8_to_wide(cwd);
    wchar_t *wide_absolute;
    char *absolute;
    DWORD capacity;
    DWORD length;
    size_t absolute_bytes;

    (void)loop;
    if (wide_cwd == NULL) return NULL;
    capacity = GetFullPathNameW(wide_cwd, 0, NULL, NULL);
    absolute_bytes = (size_t)capacity * sizeof(*wide_absolute);
    if (capacity == 0 || absolute_bytes / sizeof(*wide_absolute) != (size_t)capacity) {
        free(wide_cwd);
        return NULL;
    }
    wide_absolute = malloc(absolute_bytes);
    if (wide_absolute == NULL) {
        free(wide_cwd);
        return NULL;
    }
    length = GetFullPathNameW(wide_cwd, capacity, wide_absolute, NULL);
    free(wide_cwd);
    if (length == 0 || length >= capacity) {
        free(wide_absolute);
        return NULL;
    }
    absolute = patch_wide_to_utf8(wide_absolute);
    free(wide_absolute);
    return absolute;
}
#endif

bool patch_root_resolve(
    uv_loop_t *loop,
    const uint8_t *cwd,
    size_t cwd_length,
    char **root
) {
    uv_fs_t request;
    char *input = patch_cwd_string(cwd, cwd_length);
    char *absolute;
    int result;

    if (input == NULL) return false;
    absolute = patch_root_absolute(loop, input);
    free(input);
    if (absolute == NULL) return false;
    result = uv_fs_stat(loop, &request, absolute, NULL);
    if (result == 0 && (request.statbuf.st_mode & S_IFMT) != S_IFDIR) result = UV_ENOTDIR;
    uv_fs_req_cleanup(&request);
    if (result != 0) {
        free(absolute);
        return false;
    }
    *root = absolute;
    return true;
}

static bool patch_path_separator(uint8_t byte) {
#ifdef _WIN32
    return byte == '/' || byte == '\\';
#else
    return byte == '/';
#endif
}

static bool patch_path_is_absolute(PatchSlice relative) {
    if (patch_path_separator(relative.bytes[0])) return true;
#ifdef _WIN32
    return relative.length >= 2 && relative.bytes[1] == ':';
#else
    return false;
#endif
}

static bool patch_path_check_components(
    uv_loop_t *loop,
    char *path,
    size_t root_length,
    size_t path_length
) {
    size_t cursor = root_length;

    if (cursor < path_length && patch_path_separator((uint8_t)path[cursor])) cursor++;
    while (cursor < path_length) {
        uv_fs_t request;
        size_t end = cursor;
        char saved;
        int result;

        while (end < path_length && !patch_path_separator((uint8_t)path[end])) end++;
        saved = path[end];
        path[end] = 0;
        result = uv_fs_lstat(loop, &request, path, NULL);
        if (result == 0 && (request.statbuf.st_mode & S_IFMT) == S_IFLNK) {
            result = UV_ELOOP;
        }
        uv_fs_req_cleanup(&request);
        path[end] = saved;
        if (result == UV_ENOENT) return true;
        if (result != 0) return false;
        cursor = end + 1;
    }
    return true;
}

bool patch_path_resolve(
    uv_loop_t *loop,
    const char *root,
    PatchSlice relative,
    char **path
) {
    size_t root_length;
    size_t output_length;
    size_t cursor = 0;
    char *output;
#ifdef _WIN32
    const char separator = '\\';
#else
    const char separator = '/';
#endif

    if (relative.length == 0 || relative.length == SIZE_MAX ||
        memchr(relative.bytes, 0, relative.length) != NULL ||
        patch_path_is_absolute(relative)) return false;
    root_length = strlen(root);
    if (root_length > SIZE_MAX - 2 ||
        relative.length > SIZE_MAX - root_length - 2) return false;
    output = malloc(root_length + relative.length + 2);
    if (output == NULL) return false;
    memcpy(output, root, root_length);
    output_length = root_length;

    while (cursor < relative.length) {
        size_t start;
        size_t length;

        while (cursor < relative.length && patch_path_separator(relative.bytes[cursor])) {
            cursor++;
        }
        start = cursor;
        while (cursor < relative.length && !patch_path_separator(relative.bytes[cursor])) {
            cursor++;
        }
        length = cursor - start;
        if (length == 0 ||
            (length == 1 && relative.bytes[start] == '.')) continue;
        if (length == 2 && relative.bytes[start] == '.' &&
            relative.bytes[start + 1] == '.') {
            if (output_length == root_length) {
                free(output);
                return false;
            }
            while (output_length > root_length &&
                !patch_path_separator((uint8_t)output[output_length - 1])) {
                output_length--;
            }
            if (output_length > root_length) output_length--;
            continue;
        }
        if (output_length != 0 &&
            !patch_path_separator((uint8_t)output[output_length - 1])) {
            output[output_length++] = separator;
        }
        memcpy(output + output_length, relative.bytes + start, length);
        output_length += length;
    }
    output[output_length] = 0;
#ifdef _WIN32
    if (MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, output, -1, NULL, 0
    ) <= 0) {
        free(output);
        return false;
    }
#endif
    if (!patch_path_check_components(
        loop, output, root_length, output_length
    )) {
        free(output);
        return false;
    }
    *path = output;
    return true;
}

bool patch_path_validate(uv_loop_t *loop, const char *root, char *path) {
    size_t root_length = strlen(root);
    size_t path_length = strlen(path);

    if (root_length == 0 || path_length < root_length ||
        memcmp(path, root, root_length) != 0 ||
        (path_length > root_length &&
            !patch_path_separator((uint8_t)root[root_length - 1]) &&
            !patch_path_separator((uint8_t)path[root_length]))) return false;
    return patch_path_check_components(loop, path, root_length, path_length);
}

void patch_path_release(char **path) {
    free(*path);
    *path = NULL;
}
