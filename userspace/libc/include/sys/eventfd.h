#ifndef AKAOS_LIBC_SYS_EVENTFD_H
#define AKAOS_LIBC_SYS_EVENTFD_H

#include <stdint.h>

/* Linux-compatible eventfd flags.  The descriptor itself is implemented by
 * the akaOS libc bridge and is translated to the native pipe wait ABI. */
#define EFD_CLOEXEC   0x80000
#define EFD_NONBLOCK  0x800
#define EFD_SEMAPHORE 1

typedef uint64_t eventfd_t;

#ifdef __cplusplus
extern "C" {
#endif
int eventfd(unsigned int initval, int flags);
int eventfd_read(int descriptor, eventfd_t *value);
int eventfd_write(int descriptor, eventfd_t value);
#ifdef __cplusplus
}
#endif

#endif
