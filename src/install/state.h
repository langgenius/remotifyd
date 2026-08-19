#ifndef REMOTIFY_INSTALL_STATE_H
#define REMOTIFY_INSTALL_STATE_H

#include <stdbool.h>

bool state_resolve_directory(const char *explicit_directory, char **directory);
bool state_load(
    const char *directory,
    char **api_url,
    char **device_credential
);
bool state_save(
    const char *directory,
    const char *api_url,
    const char *device_credential
);

#endif
