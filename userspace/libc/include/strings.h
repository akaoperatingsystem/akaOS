#ifndef AKAOS_LIBC_STRINGS_H
#define AKAOS_LIBC_STRINGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int strcasecmp(const char *left, const char *right);
int strncasecmp(const char *left, const char *right, size_t maximum_length);

#ifdef __cplusplus
}
#endif

#endif
