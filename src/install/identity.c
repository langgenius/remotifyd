#include "install/identity.h"

#include "fs/atomic_replace.h"
#include "fs/open.h"
#include "runtime.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static bool identity_path(const char *directory, char path[PATH_MAX]) {
    int length = snprintf(path, PATH_MAX, "%s/device_code.json", directory);
    return length > 0 && length < PATH_MAX;
}

static bool load_code(const char *path, char **device_code, bool *missing) {
    FILE *file = fs_open(path, "rb");
    yyjson_doc *document;
    yyjson_val *value;

    *missing = false;
    if (file == NULL) {
        *missing = errno == ENOENT;
        return false;
    }
    document = yyjson_read_fp(file, 0, NULL, NULL);
    fclose(file);
    value = document == NULL ? NULL
        : yyjson_obj_get(yyjson_doc_get_root(document), "deviceCode");
    if (yyjson_is_str(value) && yyjson_get_len(value) != 0) {
        *device_code = strdup(yyjson_get_str(value));
    }
    yyjson_doc_free(document);
    return *device_code != NULL;
}

static bool save_code(const char *path, const char *device_code) {
    AtomicReplace replacement = {0};
    char body[256];
    int length = snprintf(body, sizeof(body), "{\n  \"deviceCode\": \"%s\"\n}\n", device_code);
    bool saved = length > 0 && (size_t)length < sizeof(body) &&
        atomic_replace_prepare(path, &replacement) &&
        fwrite(body, 1, (size_t)length, atomic_replace_file(&replacement)) == (size_t)length &&
        atomic_replace_commit(&replacement, path);

    atomic_replace_abort(&replacement);
    return saved;
}

bool identity_hostname(char **hostname) {
    char value[256] = {0};

    if (gethostname(value, sizeof(value) - 1) != 0) {
        return false;
    }
    *hostname = strdup(value);
    return *hostname != NULL;
}

static char *new_code(void) {
    static const char hexadecimal[] = "0123456789abcdef";
    unsigned char random[12];
    char *hostname = NULL;
    char slug[33] = {0};
    char *code = NULL;
    size_t slug_length = 0;
    bool separator = true;

    if (!runtime_random(random, sizeof(random)) || !identity_hostname(&hostname)) {
        goto cleanup;
    }
    for (const unsigned char *cursor = (const unsigned char *)hostname;
         *cursor != '\0' && slug_length < sizeof(slug) - 1;
         cursor++) {
        unsigned char character = (unsigned char)tolower(*cursor);
        if (isalnum(character)) {
            slug[slug_length++] = (char)character;
            separator = false;
        } else if (!separator) {
            slug[slug_length++] = '-';
            separator = true;
        }
    }
    while (slug_length != 0 && slug[slug_length - 1] == '-') {
        slug[--slug_length] = '\0';
    }
    if (slug_length == 0) {
        memcpy(slug, "device", 7);
    }
    code = malloc(strlen("cli--") + strlen(slug) + sizeof(random) * 2 + 1);
    if (code != NULL) {
        sprintf(code, "cli-%s-", slug);
        size_t offset = strlen(code);
        for (size_t index = 0; index < sizeof(random); index++) {
            code[offset + index * 2] = hexadecimal[random[index] >> 4];
            code[offset + index * 2 + 1] = hexadecimal[random[index] & 15];
        }
        code[offset + sizeof(random) * 2] = '\0';
    }

cleanup:
    free(hostname);
    return code;
}

bool identity_load_or_create(const char *directory, char **device_code) {
    char path[PATH_MAX];
    bool missing;

    *device_code = NULL;
    if (!identity_path(directory, path)) {
        return false;
    }
    if (load_code(path, device_code, &missing)) {
        return true;
    }
    /* Corrupt identity is an error; silently replacing it would create a new Device. */
    if (!missing || (*device_code = new_code()) == NULL ||
        !save_code(path, *device_code)) {
        free(*device_code);
        *device_code = NULL;
        return false;
    }
    return true;
}

const char *identity_platform(void) {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}
