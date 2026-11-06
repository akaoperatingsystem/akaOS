#ifndef AKAOS_LIBC_SYS_RESOURCE_H
#define AKAOS_LIBC_SYS_RESOURCE_H

#include <sys/time.h>
#include <sys/types.h>

typedef unsigned long rlim_t;
#define RLIM_INFINITY ((rlim_t)-1)
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN (-1)
#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_NOFILE 7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS 9
struct rlimit { rlim_t rlim_cur; rlim_t rlim_max; };
struct rusage {
    struct timeval ru_utime, ru_stime;
    long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss;
    long ru_minflt, ru_majflt, ru_nswap, ru_inblock, ru_oublock;
    long ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw;
};
#ifdef __cplusplus
extern "C" {
#endif
int getrusage(int who, struct rusage *usage);
int getrlimit(int resource, struct rlimit *limit);
int setrlimit(int resource, const struct rlimit *limit);
#ifdef __cplusplus
}
#endif
#endif
