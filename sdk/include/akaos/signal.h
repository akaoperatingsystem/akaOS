#ifndef AKAOS_SIGNAL_H
#define AKAOS_SIGNAL_H

#include <stdint.h>

/* Signal numbers shared by XSCore and native userspace. */
#define AKAOS_SIGHUP 1
#define AKAOS_SIGINT 2
#define AKAOS_SIGQUIT 3
#define AKAOS_SIGILL 4
#define AKAOS_SIGTRAP 5
#define AKAOS_SIGABRT 6
#define AKAOS_SIGBUS 7
#define AKAOS_SIGFPE 8
#define AKAOS_SIGKILL 9
#define AKAOS_SIGUSR1 10
#define AKAOS_SIGSEGV 11
#define AKAOS_SIGUSR2 12
#define AKAOS_SIGPIPE 13
#define AKAOS_SIGALRM 14
#define AKAOS_SIGTERM 15
#define AKAOS_SIGCHLD 17
#define AKAOS_SIGCONT 18
#define AKAOS_SIGSTOP 19
#define AKAOS_SIGTSTP 20
#define AKAOS_SIGTTIN 21
#define AKAOS_SIGTTOU 22
#define AKAOS_SIGURG 23
#define AKAOS_SIGXCPU 24
#define AKAOS_SIGXFSZ 25
#define AKAOS_SIGVTALRM 26
#define AKAOS_SIGPROF 27
#define AKAOS_SIGWINCH 28
#define AKAOS_SIGIO 29
#define AKAOS_SIGPWR 30
#define AKAOS_SIGSYS 31

#define AKAOS_SIG_DFL ((void (*)(int))0)
#define AKAOS_SIG_IGN ((void (*)(int))1)
#define AKAOS_SIG_ERR ((void (*)(int))-1)
#define AKAOS_SA_SIGINFO UINT32_C(0x00000004)
#define SA_RESTART   UINT32_C(0x10000000)
#define SA_NODEFER   UINT32_C(0x40000000)
#define SA_RESETHAND UINT32_C(0x80000000)
#define SA_NOCLDSTOP UINT32_C(0x00000001)
#define AKAOS_SI_USER 0
#define AKAOS_SI_KERNEL 0x80
#define AKAOS_BUS_ADRERR 2
#define AKAOS_SIG_BLOCK 0
#define AKAOS_SIG_UNBLOCK 1
#define AKAOS_SIG_SETMASK 2

struct akaos_siginfo {
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    uint32_t reserved;
    void *si_addr;
    uint8_t padding[128U - 16U - sizeof(uintptr_t)];
};

struct akaos_sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, struct akaos_siginfo *, void *);
    };
    uint32_t sa_mask;
    uint32_t sa_flags;
    void (*sa_restorer)(void);
};

#define AKAOS_SIGNAL_MIN 1
#define AKAOS_SIGNAL_MAX 31

#endif
