#ifndef _STDLIB_H_
#define _STDLIB_H_

#include <stddef.h>
#include <stdint.h>

#define RAND_MAX 32767

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);

void exit(int status);
int atoi(const char *nptr);
long atol(const char *nptr);
int abs(int j);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
char *getenv(const char *name);
int rand(void);
void srand(unsigned int seed);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void abort(void);
int atexit(void (*function)(void));

#endif
