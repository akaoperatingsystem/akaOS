#ifndef AKAOS_ASSERT_H
#define AKAOS_ASSERT_H

#include <stdlib.h>

#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert
#endif

#ifdef __cplusplus
extern "C" {
#endif

void __assert_fail(const char *assertion, const char *file,
                   unsigned int line, const char *function) __attribute__((__noreturn__));

#ifdef __cplusplus
}
#endif

#ifndef NDEBUG
#define assert(expression) ((expression) ? (void)0 : abort())
#else
#define assert(expression) ((void)0)
#endif

#endif
