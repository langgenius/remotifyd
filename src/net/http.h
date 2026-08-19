#ifndef REMOTIFY_NET_HTTP_H
#define REMOTIFY_NET_HTTP_H

#include <stdbool.h>

#include <curl/curl.h>

bool http_configure(CURL *request);

#endif
