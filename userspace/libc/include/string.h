#ifndef AKAOS_LIBC_STRING_H
#define AKAOS_LIBC_STRING_H

#include <features.h>
#include <stddef.h>
#include <stdint.h>
#include <locale.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t strlen(const char *text);
size_t strnlen(const char *text, size_t maximum_length);
size_t strspn(const char *text, const char *accept);
size_t strcspn(const char *text, const char *reject);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t n);
int strcasecmp(const char *left, const char *right);
int strncasecmp(const char *left, const char *right, size_t n);
void bzero(void *s, size_t n);
void bcopy(const void *src, void *dest, size_t n);
int bcmp(const void *s1, const void *s2, size_t n);
int strcoll(const char *left, const char *right);
size_t strxfrm(char *destination, const char *source, size_t count);
int strcoll_l(const char *left, const char *right, locale_t locale);
size_t strxfrm_l(char *destination, const char *source, size_t count,
                 locale_t locale);
char *strncpy(char *destination, const char *source, size_t n);
char *strcpy(char *destination, const char *source);
char *strcat(char *destination, const char *source);
char *strncat(char *destination, const char *source, size_t n);
char *strdup(const char *source);
char *strndup(const char *source, size_t maximum_length);
char *strsep(char **stringp, const char *delimiters);
char *strtok(char *text, const char *delimiters);
char *strtok_r(char *text, const char *delimiters, char **save_pointer);

char *strchr(const char *text, int character);
char *strrchr(const char *text, int character);
char *strstr(const char *haystack, const char *needle);
char *strpbrk(const char *text, const char *accept);
int memcmp(const void *left, const void *right, size_t length);

void *memcpy(void *destination, const void *source, size_t length);
void *memmove(void *destination, const void *source, size_t length);
void *memset(void *destination, int value, size_t length);
void *memchr(const void *s, int c, size_t n);
char *strerror(int errnum);
int strerror_r(int errnum, char *buffer, size_t capacity);
const char *strsignal(int sig);

#ifdef __cplusplus
}
#endif

#endif
