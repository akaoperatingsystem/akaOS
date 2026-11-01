#ifndef _STDLIB_H
#define _STDLIB_H

#include <features.h>
#include <stddef.h>
#include <locale.h>
#include <limits.h>

#ifndef RAND_MAX
#define RAND_MAX INT_MAX
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long quot;
    long rem;
} ldiv_t;

typedef struct {
    long long quot;
    long long rem;
} lldiv_t;

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void free(void *ptr);
void qsort(void *base, size_t nmemb, size_t size,
           int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compare)(const void *, const void *));
char *realpath(const char *path, char *resolved_path);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void *aligned_alloc(size_t alignment, size_t size);
int posix_memalign(void **memptr, size_t alignment, size_t size);
int mkstemp(char *template_path);

int atoi(const char *nptr);
double atof(const char *nptr);
int rand(void);
void srand(unsigned int seed);
int rand_r(unsigned int *seed);
long random(void);
void srandom(unsigned int seed);
long atol(const char *nptr);
long long atoll(const char *nptr);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
float strtof(const char *nptr, char **endptr);
double strtod(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
float strtof_l(const char *nptr, char **endptr, locale_t locale);
double strtod_l(const char *nptr, char **endptr, locale_t locale);
long double strtold_l(const char *nptr, char **endptr, locale_t locale);
long long strtoll_l(const char *nptr, char **endptr, int base, locale_t locale);
unsigned long long strtoull_l(const char *nptr, char **endptr, int base,
                              locale_t locale);
int abs(int value);
long labs(long value);
long long llabs(long long value);
div_t div(int numerator, int denominator);
ldiv_t ldiv(long numerator, long denominator);
lldiv_t lldiv(long long numerator, long long denominator);
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *string);
int clearenv(void);
size_t __akaos_mb_cur_max(void);

/* The legacy X/Open spelling of the multibyte length operation.  Upstream
 * X11 sources call it directly. */
int mblen(const char *multibyte, size_t length);
void abort(void) __attribute__((noreturn));
void exit(int status) __attribute__((noreturn));
int atexit(void (*function)(void));
int system(const char *command);
int mbtowc(wchar_t *wide_character, const char *multibyte, size_t length);
int wctomb(char *multibyte, wchar_t wide_character);
size_t mbstowcs(wchar_t *wide, const char *multibyte, size_t count);
size_t wcstombs(char *multibyte, const wchar_t *wide, size_t count);

#ifdef __cplusplus
}
#endif

#ifndef MB_CUR_MAX
#define MB_CUR_MAX (__akaos_mb_cur_max())
#endif

#endif
