#ifndef AKAOS_LIBC_SIGNAL_H
#define AKAOS_LIBC_SIGNAL_H

#include <akaos/signal.h>

typedef uint32_t sigset_t;
typedef int sig_atomic_t;

#define NSIG 32
#define SIG_BLOCK AKAOS_SIG_BLOCK
#define SIG_UNBLOCK AKAOS_SIG_UNBLOCK
#define SIG_SETMASK AKAOS_SIG_SETMASK
#define SA_SIGINFO AKAOS_SA_SIGINFO
#define SI_USER AKAOS_SI_USER
#define SI_KERNEL AKAOS_SI_KERNEL

#ifndef CLD_EXITED
#define CLD_EXITED 1
#define CLD_KILLED 2
#define CLD_DUMPED 3
#define CLD_TRAPPED 4
#define CLD_STOPPED 5
#define CLD_CONTINUED 6
#endif
#define BUS_ADRERR AKAOS_BUS_ADRERR

typedef struct akaos_siginfo siginfo_t;

#define SIGHUP   AKAOS_SIGHUP
#define SIGINT   AKAOS_SIGINT
#define SIGQUIT  AKAOS_SIGQUIT
#define SIGILL   AKAOS_SIGILL
#define SIGTRAP  AKAOS_SIGTRAP
#define SIGABRT  AKAOS_SIGABRT
#define SIGBUS   AKAOS_SIGBUS
#define SIGFPE   AKAOS_SIGFPE
#define SIGKILL  AKAOS_SIGKILL
#define SIGUSR1  AKAOS_SIGUSR1
#define SIGSEGV  AKAOS_SIGSEGV
#define SIGUSR2  AKAOS_SIGUSR2
#define SIGPIPE  AKAOS_SIGPIPE
#define SIGALRM  AKAOS_SIGALRM
#define SIGTERM  AKAOS_SIGTERM
#define SIGCHLD  AKAOS_SIGCHLD
#define SIGCONT  AKAOS_SIGCONT
#define SIGSTOP  AKAOS_SIGSTOP
#define SIGTSTP  AKAOS_SIGTSTP
#define SIGTTIN  AKAOS_SIGTTIN
#define SIGTTOU  AKAOS_SIGTTOU
#define SIGURG   AKAOS_SIGURG
#define SIGXCPU  AKAOS_SIGXCPU
#define SIGXFSZ  AKAOS_SIGXFSZ
#define SIGVTALRM AKAOS_SIGVTALRM
#define SIGPROF  AKAOS_SIGPROF
#define SIGWINCH AKAOS_SIGWINCH
#define SIGIO    AKAOS_SIGIO
#define SIGPWR   AKAOS_SIGPWR
#define SIGSYS   AKAOS_SIGSYS

typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    };
    uint32_t sa_mask;
    uint32_t sa_flags;
    void (*sa_restorer)(void);
};
typedef struct sigaction sigaction_t;

#ifdef __cplusplus
extern "C" {
#endif

int kill(int pid, int signal);
int killpg(int pgrp, int signal);
int raise(int sig);
sighandler_t signal(int signal_number, sighandler_t handler);
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signal_number);
int sigdelset(sigset_t *set, int signal_number);
int sigismember(const sigset_t *set, int signal_number);
int sigprocmask(int how, const sigset_t *set, sigset_t *old_set);
int sigreturn(void);
void __akaos_sigreturn_trampoline(void);
const char *strsignal(int sig);

#ifdef __cplusplus
}
#endif

#endif
