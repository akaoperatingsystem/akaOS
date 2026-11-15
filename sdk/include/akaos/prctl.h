#ifndef AKAOS_PRCTL_H
#define AKAOS_PRCTL_H

#include <stdint.h>

#define AKAOS_PR_SET_NAME        15
#define AKAOS_PR_GET_NAME        16
#define AKAOS_PR_GET_SECCOMP     21
#define AKAOS_PR_SET_SECCOMP     22
#define AKAOS_PR_SET_NO_NEW_PRIVS 38
#define AKAOS_PR_GET_NO_NEW_PRIVS 39

#endif /* AKAOS_PRCTL_H */
