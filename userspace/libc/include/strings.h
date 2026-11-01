#ifndef AKAOS_LIBC_STRINGS_H
#define AKAOS_LIBC_STRINGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int strcasecmp(const char *left, const char *right);
int strncasecmp(const char *left, const char *right, size_t maximum_length);
void bzero(void *s, size_t n);
void bcopy(const void *src, void *dest, size_t n);
int bcmp(const void *s1, const void *s2, size_t n);

#ifdef __cplusplus
}
#endif

#endif
