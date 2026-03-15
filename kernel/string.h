/* ============================================================
 * akaOS — String Utilities Header
 * ============================================================ */

#ifndef STRING_H
#define STRING_H

#include <stdint.h>
#include <stddef.h>

size_t  strlen(const char *str);
int     strcmp(const char *s1, const char *s2);
int     strncmp(const char *s1, const char *s2, size_t n);
char   *strcpy(char *dest, const char *src);
char   *strncpy(char *dest, const char *src, size_t n);
void   *memset(void *ptr, int value, size_t num);
void   *memcpy(void *dest, const void *src, size_t num);
void   *memmove(void *dest, const void *src, size_t n);
int     memcmp(const void *s1, const void *s2, size_t n);

char   *strcat(char *dest, const char *src);
char   *strncat(char *dest, const char *src, size_t n);
char   *strchr(const char *str, int c);
char   *strrchr(const char *str, int c);
char   *strstr(const char *haystack, const char *needle);
char   *strdup(const char *s);
char   *strtok(char *str, const char *delim);

/* Check if string starts with prefix */
int     starts_with(const char *str, const char *prefix);

/* Convert integer to string */
void    int_to_str(int num, char *buf);

#endif /* STRING_H */
