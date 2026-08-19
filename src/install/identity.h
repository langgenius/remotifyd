#ifndef REMOTIFY_INSTALL_IDENTITY_H
#define REMOTIFY_INSTALL_IDENTITY_H

#include <stdbool.h>

bool identity_load_or_create(const char *directory, char **device_code);
bool identity_hostname(char **hostname);
const char *identity_platform(void);

#endif
