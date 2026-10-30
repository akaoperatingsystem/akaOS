#ifndef AKAOS_LIBC_SYS_FILE_H
#define AKAOS_LIBC_SYS_FILE_H

#include <akaos/syscall.h>

#define LOCK_SH AKAOS_LOCK_SH
#define LOCK_EX AKAOS_LOCK_EX
#define LOCK_NB AKAOS_LOCK_NB
#define LOCK_UN AKAOS_LOCK_UN

#ifdef __cplusplus
extern "C" {
#endif

int flock(int descriptor, int operation);

#ifdef __cplusplus
}
#endif

#endif
