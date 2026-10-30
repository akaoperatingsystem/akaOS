#ifndef AKAOS_LIBC_WCHAR_H
#define AKAOS_LIBC_WCHAR_H

#include <stddef.h>
#include <stdint.h>
#include <locale.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    uint32_t value;
} mbstate_t;
typedef uint32_t wint_t;

#ifndef WEOF
#define WEOF ((wint_t)-1)
#endif

#ifndef WCHAR_MIN
#define WCHAR_MIN 0U
#endif
#ifndef WCHAR_MAX
#define WCHAR_MAX UINT32_MAX
#endif

#ifdef __cplusplus
extern "C" {
#endif

wint_t fgetwc(FILE *stream);
wint_t getwc(FILE *stream);
wint_t getwchar(void);
wint_t ungetwc(wint_t character, FILE *stream);
wint_t fputwc(wchar_t character, FILE *stream);
wint_t putwc(wchar_t character, FILE *stream);
wint_t putwchar(wchar_t character);
wchar_t *fgetws(wchar_t *buffer, int capacity, FILE *stream);
int fputws(const wchar_t *text, FILE *stream);
int fwide(FILE *stream, int mode);

size_t mbrtowc(wchar_t *wide_character, const char *multibyte,
               size_t length, mbstate_t *state);
size_t wcrtomb(char *multibyte, wchar_t wide_character, mbstate_t *state);
size_t mbstowcs(wchar_t *wide, const char *multibyte, size_t count);
size_t wcstombs(char *multibyte, const wchar_t *wide, size_t count);
size_t mbrlen(const char *multibyte, size_t length, mbstate_t *state);
int mbtowc(wchar_t *wide_character, const char *multibyte, size_t length);
wint_t btowc(int character);
int wctob(wint_t character);
size_t mbsnrtowcs(wchar_t *wide, const char **multibyte, size_t bytes,
                  size_t count, mbstate_t *state);
size_t wcsnrtombs(char *multibyte, const wchar_t **wide, size_t count,
                  size_t bytes, mbstate_t *state);
size_t mbsrtowcs(wchar_t *wide, const char **multibyte, size_t count,
                 mbstate_t *state);
size_t wcsrtombs(char *multibyte, const wchar_t **wide, size_t bytes,
                 mbstate_t *state);
size_t wcsftime(wchar_t *destination, size_t capacity,
                const wchar_t *format, const struct tm *value);
int swprintf(wchar_t *destination, size_t capacity,
             const wchar_t *format, ...);
int vswprintf(wchar_t *destination, size_t capacity,
              const wchar_t *format, va_list ap);
long wcstol(const wchar_t *text, wchar_t **endptr, int base);
unsigned long wcstoul(const wchar_t *text, wchar_t **endptr, int base);
long long wcstoll(const wchar_t *text, wchar_t **endptr, int base);
unsigned long long wcstoull(const wchar_t *text, wchar_t **endptr, int base);
float wcstof(const wchar_t *text, wchar_t **endptr);
double wcstod(const wchar_t *text, wchar_t **endptr);
long double wcstold(const wchar_t *text, wchar_t **endptr);
size_t wcslen(const wchar_t *wide);
int wcscmp(const wchar_t *left, const wchar_t *right);
int wcsncmp(const wchar_t *left, const wchar_t *right, size_t count);
int wcscoll(const wchar_t *left, const wchar_t *right);
size_t wcsxfrm(wchar_t *destination, const wchar_t *source, size_t count);
int wcscoll_l(const wchar_t *left, const wchar_t *right, locale_t locale);
size_t wcsxfrm_l(wchar_t *destination, const wchar_t *source, size_t count,
                 locale_t locale);
wchar_t *wcschr(const wchar_t *wide, wchar_t character);
wchar_t *wcsrchr(const wchar_t *wide, wchar_t character);
wchar_t *wcsstr(const wchar_t *wide, const wchar_t *needle);
wchar_t *wcscpy(wchar_t *destination, const wchar_t *source);
wchar_t *wcsncpy(wchar_t *destination, const wchar_t *source, size_t count);
wchar_t *wcscat(wchar_t *destination, const wchar_t *source);
wchar_t *wcsncat(wchar_t *destination, const wchar_t *source, size_t count);
wchar_t *wcspbrk(const wchar_t *wide, const wchar_t *accept);
size_t wcscspn(const wchar_t *wide, const wchar_t *reject);
size_t wcsspn(const wchar_t *wide, const wchar_t *accept);
wchar_t *wmemchr(const wchar_t *wide, wchar_t character, size_t count);
wchar_t *wmemcpy(wchar_t *destination, const wchar_t *source, size_t count);
wchar_t *wmemmove(wchar_t *destination, const wchar_t *source, size_t count);
wchar_t *wmemset(wchar_t *wide, wchar_t character, size_t count);
int wmemcmp(const wchar_t *left, const wchar_t *right, size_t count);
int iswalnum(wint_t character);
int iswalpha(wint_t character);
int iswdigit(wint_t character);
int iswspace(wint_t character);
int towlower(wint_t character);
int towupper(wint_t character);
int iswspace_l(wint_t character, locale_t locale);

#ifdef __cplusplus
}
#endif

#endif
