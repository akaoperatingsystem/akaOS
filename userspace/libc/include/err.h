#ifndef AKAOS_LIBC_ERR_H
#define AKAOS_LIBC_ERR_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void err(int eval, const char *fmt, ...) __attribute__((__noreturn__, __format__(__printf__, 2, 3)));
void errx(int eval, const char *fmt, ...) __attribute__((__noreturn__, __format__(__printf__, 2, 3)));
void warn(const char *fmt, ...) __attribute__((__format__(__printf__, 1, 2)));
void warnx(const char *fmt, ...) __attribute__((__format__(__printf__, 1, 2)));
void verr(int eval, const char *fmt, va_list args) __attribute__((__noreturn__));
void verrx(int eval, const char *fmt, va_list args) __attribute__((__noreturn__));
void vwarn(const char *fmt, va_list args);
void vwarnx(const char *fmt, va_list args);

#ifdef __cplusplus
}
#endif

#endif /* AKAOS_LIBC_ERR_H */
