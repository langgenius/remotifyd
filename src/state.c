#include "state.h"

#include "logger.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
static const char state_path_suffix[] = "\\.remotify\\remotifyd.bin";
#else
#include <sys/stat.h>
static const char state_path_suffix[] = "/.remotify/remotifyd.bin";
#endif

/*
 * The file starts with one fixed 40-byte header followed by the raw api_base,
 * token, and device_code bytes. Every index is relative to the first body byte,
 * so a later header version can grow without changing stored indexes.
 *
 * Persistent integers are little-endian uint32_t values rather than size_t;
 * this keeps the same offsets on 32-bit Windows and 64-bit systems.
 */
enum {
    STATE_MAGIC_OFFSET = 0,
    STATE_MAGIC_LENGTH = 8,
    STATE_VERSION_OFFSET = 8,
    STATE_HEADER_LENGTH_OFFSET = 12,
    STATE_API_BASE_INDEX_OFFSET = 16,
    STATE_API_BASE_LENGTH_OFFSET = 20,
    STATE_TOKEN_INDEX_OFFSET = 24,
    STATE_TOKEN_LENGTH_OFFSET = 28,
    STATE_DEVICE_CODE_INDEX_OFFSET = 32,
    STATE_DEVICE_CODE_LENGTH_OFFSET = 36,
    STATE_HEADER_LENGTH = 40,
    STATE_VERSION = 1
};

static const uint8_t state_magic[STATE_MAGIC_LENGTH] = {
    0x72, 0x65, 0x6d, 0x6f,
    0x74, 0x69, 0x66, 0x79
};

#define STATE_READ_U32(bytes) ( \
    (uint32_t)((bytes)[0]) | \
    (uint32_t)((bytes)[1]) << 8 | \
    (uint32_t)((bytes)[2]) << 16 | \
    (uint32_t)((bytes)[3]) << 24 \
)

#define STATE_WRITE_U32(bytes, value) do { \
    (bytes)[0] = (uint8_t)(value); \
    (bytes)[1] = (uint8_t)((value) >> 8); \
    (bytes)[2] = (uint8_t)((value) >> 16); \
    (bytes)[3] = (uint8_t)((value) >> 24); \
} while (0)

static char *state_path(void) {
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
#else
    const char *home = getenv("HOME");
#endif
    char *path;

    if (home == NULL || home[0] == '\0') {
        return NULL;
    }

    path = malloc(strlen(home) + sizeof(state_path_suffix));
    sprintf(path, "%s%s", home, state_path_suffix);
    return path;
}

static bool state_prepare_directory(char *path) {
#ifdef _WIN32
    char *separator = strrchr(path, '\\');
#else
    char *separator = strrchr(path, '/');
#endif
    char value = *separator;
    int result;

    *separator = '\0';

#ifdef _WIN32
    result = _mkdir(path);
#else
    result = mkdir(path, 0700);
#endif

    *separator = value;
    return result == 0 || errno == EEXIST;
}

static bool state_empty(State *state) {
    state->api_base = calloc(1, 1);
    state->token = calloc(1, 1);
    state->device_code = calloc(1, 1);

    if (state->api_base != NULL &&
        state->token != NULL &&
        state->device_code != NULL) {
        return true;
    }

    state_free(state);
    return false;
}

void state_free(State *state) {
    free(state->api_base);
    free(state->token);
    free(state->device_code);
    *state = (State){0};
}

bool state_load(State *state) {
    uint8_t header[STATE_HEADER_LENGTH];
    uint32_t header_length;
    uint32_t api_base_index;
    uint32_t api_base_length;
    uint32_t token_index;
    uint32_t token_length;
    uint32_t device_code_index;
    uint32_t device_code_length;
    size_t body_length;
    long file_length;
    int open_error;
    FILE *file;
    char *path;

    *state = (State){0};
    path = state_path();

    if (path == NULL) return false;

    file = fopen(path, "rb");
    open_error = errno;
    free(path);

    if (file == NULL) {
        return open_error == ENOENT && state_empty(state);
    }

    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_length = ftell(file)) < STATE_HEADER_LENGTH ||
        fseek(file, 0, SEEK_SET) != 0 ||
        fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return false;
    }

    header_length = STATE_READ_U32(header + STATE_HEADER_LENGTH_OFFSET);

    if (memcmp(header + STATE_MAGIC_OFFSET, state_magic, STATE_MAGIC_LENGTH) != 0 ||
        STATE_READ_U32(header + STATE_VERSION_OFFSET) != STATE_VERSION ||
        header_length != STATE_HEADER_LENGTH) {
        fclose(file);
        return false;
    }

    api_base_index = STATE_READ_U32(header + STATE_API_BASE_INDEX_OFFSET);
    api_base_length = STATE_READ_U32(header + STATE_API_BASE_LENGTH_OFFSET);
    token_index = STATE_READ_U32(header + STATE_TOKEN_INDEX_OFFSET);
    token_length = STATE_READ_U32(header + STATE_TOKEN_LENGTH_OFFSET);
    device_code_index = STATE_READ_U32(header + STATE_DEVICE_CODE_INDEX_OFFSET);
    device_code_length = STATE_READ_U32(header + STATE_DEVICE_CODE_LENGTH_OFFSET);
    body_length = (size_t)file_length - header_length;

    if ((size_t)api_base_index > body_length ||
        (size_t)api_base_length > body_length - api_base_index ||
        (size_t)token_index > body_length ||
        (size_t)token_length > body_length - token_index ||
        (size_t)device_code_index > body_length ||
        (size_t)device_code_length > body_length - device_code_index) {
        fclose(file);
        return false;
    }

    state->api_base = malloc((size_t)api_base_length + 1);
    state->token = malloc((size_t)token_length + 1);
    state->device_code = malloc((size_t)device_code_length + 1);

    if (state->api_base == NULL ||
        state->token == NULL ||
        state->device_code == NULL) {
        fclose(file);
        state_free(state);
        return false;
    }

    /* Strings have no trailing zero in the body; memory strings add it here. */
    if (fseek(file, (long)header_length + api_base_index, SEEK_SET) != 0 ||
        fread(state->api_base, 1, api_base_length, file) != api_base_length ||
        fseek(file, (long)header_length + token_index, SEEK_SET) != 0 ||
        fread(state->token, 1, token_length, file) != token_length ||
        fseek(file, (long)header_length + device_code_index, SEEK_SET) != 0 ||
        fread(state->device_code, 1, device_code_length, file) != device_code_length) {
        fclose(file);
        state_free(state);
        return false;
    }

    fclose(file);
    state->api_base[api_base_length] = '\0';
    state->token[token_length] = '\0';
    state->device_code[device_code_length] = '\0';
    return true;
}

bool state_save(const State *state) {
    uint8_t header[STATE_HEADER_LENGTH] = {0};
    uint32_t api_base_length = (uint32_t)strlen(state->api_base);
    uint32_t token_length = (uint32_t)strlen(state->token);
    uint32_t device_code_length = (uint32_t)strlen(state->device_code);
    FILE *file;
    char *path;

    memcpy(header + STATE_MAGIC_OFFSET, state_magic, STATE_MAGIC_LENGTH);
    STATE_WRITE_U32(header + STATE_VERSION_OFFSET, STATE_VERSION);
    STATE_WRITE_U32(header + STATE_HEADER_LENGTH_OFFSET, STATE_HEADER_LENGTH);
    STATE_WRITE_U32(header + STATE_API_BASE_INDEX_OFFSET, 0);
    STATE_WRITE_U32(header + STATE_API_BASE_LENGTH_OFFSET, api_base_length);
    STATE_WRITE_U32(header + STATE_TOKEN_INDEX_OFFSET, api_base_length);
    STATE_WRITE_U32(header + STATE_TOKEN_LENGTH_OFFSET, token_length);
    STATE_WRITE_U32(header + STATE_DEVICE_CODE_INDEX_OFFSET, api_base_length + token_length);
    STATE_WRITE_U32(header + STATE_DEVICE_CODE_LENGTH_OFFSET, device_code_length);

    path = state_path();

    if (path == NULL) {
        logf_error("failed to locate the user home directory");
        return false;
    }

    if (!state_prepare_directory(path)) {
        logf_error("failed to create the state directory");
        free(path);
        return false;
    }

    file = fopen(path, "wb");
    free(path);

    if (file == NULL) {
        logf_error("failed to open state file for writing");
        return false;
    }

    if (fwrite(header, 1, sizeof(header), file) != sizeof(header) ||
        fwrite(state->api_base, 1, api_base_length, file) != api_base_length ||
        fwrite(state->token, 1, token_length, file) != token_length ||
        fwrite(state->device_code, 1, device_code_length, file) != device_code_length) {
        logf_error("failed to write state file");
        fclose(file);
        return false;
    }

    if (fclose(file) != 0) {
        logf_error("failed to finish state file");
        return false;
    }

    return true;
}
