#ifndef _SYS_PRCTL_H
#define _SYS_PRCTL_H

#include <features.h>

#define PR_SET_PDEATHSIG 1
#define PR_GET_PDEATHSIG 2
#define PR_GET_DUMPABLE  3
#define PR_SET_DUMPABLE  4
#define PR_SET_NAME      15
#define PR_GET_NAME      16
#define PR_GET_SECCOMP   21
#define PR_SET_SECCOMP   22
#define PR_SET_TIMING    14
#define PR_GET_TIMING    13
#define PR_SET_NO_NEW_PRIVS 38
#define PR_GET_NO_NEW_PRIVS 39
#define PR_SET_PTRACER   0x59616d61

#ifdef __cplusplus
extern "C" {
#endif

int prctl(int option, ...);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_PRCTL_H */
