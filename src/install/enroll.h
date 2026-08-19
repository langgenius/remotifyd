#ifndef REMOTIFY_INSTALL_ENROLL_H
#define REMOTIFY_INSTALL_ENROLL_H

int enroll_device(
    const char *api_url,
    const char *token,
    const char *name,
    const char *state_directory
);

#endif
