#ifndef AKAOS_LIBC_LIMITS_H
#define AKAOS_LIBC_LIMITS_H

#include <stdint.h>
#include <sys/types.h>

#define CHAR_BIT __CHAR_BIT__
#define SCHAR_MIN (-__SCHAR_MAX__ - 1)
#define SCHAR_MAX __SCHAR_MAX__
#define UCHAR_MAX (__SCHAR_MAX__ * 2U + 1U)
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#define SHRT_MIN (-__SHRT_MAX__ - 1)
#define SHRT_MAX __SHRT_MAX__
#define USHRT_MAX (__SHRT_MAX__ * 2U + 1U)
#define INT_MIN (-__INT_MAX__ - 1)
#define INT_MAX __INT_MAX__
#define UINT_MAX (__INT_MAX__ * 2U + 1U)
#define LONG_MIN (-__LONG_MAX__ - 1L)
#define LONG_MAX __LONG_MAX__
#define ULONG_MAX (__LONG_MAX__ * 2UL + 1UL)
#define LLONG_MIN (-__LONG_LONG_MAX__ - 1LL)
#define LLONG_MAX __LONG_LONG_MAX__
#define ULLONG_MAX (__LONG_LONG_MAX__ * 2ULL + 1ULL)
#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(SIZE_MAX >> 1))
#endif
#ifndef PATH_MAX
#define PATH_MAX 128
#endif
#ifndef NAME_MAX
#define NAME_MAX 255
#endif
#ifndef SYMLOOP_MAX
#define SYMLOOP_MAX 8
#endif
#ifndef _POSIX_PIPE_BUF
#define _POSIX_PIPE_BUF 512
#endif
#ifndef PIPE_BUF
#define PIPE_BUF _POSIX_PIPE_BUF
#endif

#endif
