#ifndef AKAOS_LIBC_SYS_TYPES_H
#define AKAOS_LIBC_SYS_TYPES_H

#include <stdint.h>
#include <stddef.h>

typedef long ssize_t;
typedef long off_t;
typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint64_t nlink_t;
typedef long blksize_t;
typedef long long blkcnt_t;
typedef uint64_t fsblkcnt_t;
typedef uint64_t fsfilcnt_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t mode_t;
typedef int pid_t;
typedef uint32_t useconds_t;
#ifndef SSIZE_MAX
#define SSIZE_MAX ((ssize_t)(SIZE_MAX >> 1))
#endif
typedef long time_t;
#ifndef _CLOCK_T_DEFINED
#define _CLOCK_T_DEFINED
typedef long clock_t;
#endif
typedef long suseconds_t;
typedef char *caddr_t;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

#endif
