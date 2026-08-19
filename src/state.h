#ifndef REMOTIFY_STATE_H
#define REMOTIFY_STATE_H

#include <stdbool.h>

typedef struct State {
    char *api_base;
    char *token;
    char *device_code;
} State;

bool state_load(State *state);
bool state_save(const State *state);
void state_free(State *state);

#endif
