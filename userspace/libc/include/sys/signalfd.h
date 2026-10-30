#ifndef AKAOS_LIBC_SYS_SIGNALFD_H
#define AKAOS_LIBC_SYS_SIGNALFD_H

#include <akaos/signalfd.h>
#include <signal.h>

#define SFD_CLOEXEC 02000000
#define SFD_NONBLOCK 00004000

#ifdef __cplusplus
extern "C" {
#endif

int signalfd(int descriptor, const sigset_t *mask, int flags);

#ifdef __cplusplus
}
#endif

#endif
