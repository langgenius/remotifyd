#include "install/state.h"

#include "fs/atomic_replace.h"
#include "fs/open.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static bool join_path(
    const char *directory,
    const char *name,
    char result[PATH_MAX]
) {
    int length = snprintf(result, PATH_MAX, "%s/%s", directory, name);
    return length > 0 && length < PATH_MAX;
}

bool state_load(
    const char *directory,
    char **loaded_api_url,
    char **loaded_device_credential
) {
    char path[PATH_MAX];
    yyjson_doc *document = NULL;
    yyjson_val *root;
    yyjson_val *api_url;
    yyjson_val *credential;
    bool loaded = false;

    *loaded_api_url = NULL;
    *loaded_device_credential = NULL;
    if (!join_path(directory, "state.json", path)) {
        goto cleanup;
    }
    FILE *file = fs_open(path, "rb");
    if (file == NULL) {
        goto cleanup;
    }
    document = yyjson_read_fp(file, YYJSON_READ_NOFLAG, NULL, NULL);
    fclose(file);
    if (document == NULL) {
        goto cleanup;
    }
    root = yyjson_doc_get_root(document);
    api_url = yyjson_obj_get(root, "api_url");
    credential = yyjson_obj_get(root, "credential");
    if (!yyjson_is_str(api_url) || !yyjson_is_str(credential) ||
        yyjson_get_len(api_url) == 0 || yyjson_get_len(credential) == 0) {
        goto cleanup;
    }
    *loaded_api_url = strdup(yyjson_get_str(api_url));
    *loaded_device_credential = strdup(yyjson_get_str(credential));
    loaded = *loaded_api_url != NULL && *loaded_device_credential != NULL;

cleanup:
    yyjson_doc_free(document);
    if (!loaded) {
        free(*loaded_api_url);
        free(*loaded_device_credential);
        *loaded_api_url = NULL;
        *loaded_device_credential = NULL;
    }
    return loaded;
}

bool state_save(
    const char *directory,
    const char *api_url,
    const char *device_credential
) {
    char path[PATH_MAX];
    AtomicReplace replacement = {0};
    yyjson_mut_doc *document = NULL;
    yyjson_mut_val *root;
    bool saved = false;

    if (api_url == NULL || device_credential == NULL ||
        !join_path(directory, "state.json", path) ||
        !atomic_replace_prepare(path, &replacement)) {
        return false;
    }
    document = yyjson_mut_doc_new(NULL);
    root = document == NULL ? NULL : yyjson_mut_obj(document);
    if (root != NULL) {
        yyjson_mut_doc_set_root(document, root);
        saved = yyjson_mut_obj_add_str(document, root, "api_url", api_url) &&
            yyjson_mut_obj_add_str(document, root, "credential", device_credential) &&
            yyjson_mut_write_fp(
                atomic_replace_file(&replacement),
                document,
                0,
                NULL,
                NULL
            ) &&
            fputc('\n', atomic_replace_file(&replacement)) != EOF &&
            atomic_replace_commit(&replacement, path);
    }
    yyjson_mut_doc_free(document);
    atomic_replace_abort(&replacement);
    return saved;
}
