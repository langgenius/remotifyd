#ifndef REMOTIFY_FS_WINDOWS_UTF8_H
#define REMOTIFY_FS_WINDOWS_UTF8_H

#include <wchar.h>

wchar_t *windows_utf8_to_wide(const char *value);
char *windows_wide_to_utf8(const wchar_t *value);

#endif
