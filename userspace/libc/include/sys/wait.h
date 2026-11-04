#ifndef AKAOS_LIBC_SYS_WAIT_H
#define AKAOS_LIBC_SYS_WAIT_H

#include <sys/types.h>

#define WIFEXITED(status) (((status) & 0x7f) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#define WTERMSIG(status) ((status) & 0x7f)
#define WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#define WSTOPSIG(status) (((status) >> 8) & 0xff)
#define WIFCONTINUED(status) ((status) == 0xffff)

#define WNOHANG 1
#define WUNTRACED 2
#define WCONTINUED 8

#ifdef __cplusplus
extern "C" {
#endif
struct rusage;
pid_t wait(int *status);
pid_t waitpid(pid_t pid, int *status, int options);
pid_t wait3(int *status, int options, struct rusage *rusage);
pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage);
#ifdef __cplusplus
}
#endif

#endif
