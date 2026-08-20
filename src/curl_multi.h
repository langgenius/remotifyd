#ifndef REMOTIFY_CURL_MULTI_H
#define REMOTIFY_CURL_MULTI_H

#include "event_loop.h"

#include <stdbool.h>

bool init_curl_multi(EventLoop *event_loop);

#endif
