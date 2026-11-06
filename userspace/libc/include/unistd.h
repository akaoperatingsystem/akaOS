#ifndef AKAOS_LIBC_UNISTD_H
#define AKAOS_LIBC_UNISTD_H

#include <features.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* akaOS exposes CLOCK_REALTIME and CLOCK_MONOTONIC through clock_gettime(2). */
#ifndef _POSIX_TIMERS
#define _POSIX_TIMERS 200809L
#endif
#ifndef _POSIX_THREADS
#define _POSIX_THREADS 200809L
#endif
#ifndef _POSIX_THREAD_ATTR_STACKSIZE
#define _POSIX_THREAD_ATTR_STACKSIZE 200809L
#endif

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define _SC_ARG_MAX 0
#define _SC_CHILD_MAX 1
#define _SC_CLK_TCK 2
#define _SC_OPEN_MAX 4
#define _SC_GETGR_R_SIZE_MAX 69
#define _SC_GETPW_R_SIZE_MAX 70
#define _SC_PAGESIZE 30
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define _SC_NPROCESSORS_ONLN 84
#define _SC_PHYS_PAGES 85
#define _SC_AVPHYS_PAGES 86
#define _PC_PATH_MAX 4
#define _PC_NAME_MAX 3
/* The remaining pathconf names use a dedicated range so they never collide
 * with the two values above.  POSIX leaves these numbers implementation
 * defined as long as they are unique. */
#define _PC_LINK_MAX 100
#define _PC_PIPE_BUF 101
#define _PC_SYMLINK_MAX 102
#define _PC_REC_MAX_XFER 103
#define _PC_CHOWN_RESTRICTED 104
#define _PC_NO_TRUNC 105
#define _PC_VDISABLE 106
#define _PC_MAX_CANON 107
#define _PC_MAX_INPUT 108
#define _PC_2_SYMLINKS 109
#ifndef PATH_MAX
#define PATH_MAX 128
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern char **environ;
int setgroups(size_t size, const gid_t *list);

ssize_t read(int descriptor, void *buffer, size_t length);
ssize_t write(int descriptor, const void *buffer, size_t length);
ssize_t pread(int fd, void *buf, size_t count, off_t offset);
ssize_t pread64(int fd, void *buf, size_t count, off_t offset);
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
ssize_t pwrite64(int fd, const void *buf, size_t count, off_t offset);
off_t lseek(int descriptor, off_t offset, int whence);
off_t lseek64(int descriptor, off_t offset, int whence);
int close(int descriptor);
int dup(int descriptor);
int dup2(int old_descriptor, int new_descriptor);
int fchdir(int descriptor);
char *getcwd(char *buffer, size_t size);
int chdir(const char *path);
int access(const char *path, int mode);
int gethostname(char *name, size_t length);
long sysconf(int name);
int getpagesize(void);
long pathconf(const char *path, int name);

#define _CS_PATH 1
#define _CS_GNU_LIBC_VERSION 2
#define _CS_GNU_LIBPTHREAD_VERSION 3
size_t confstr(int name, char *buf, size_t len);
int unlink(const char *path);
int unlinkat(int directory, const char *path, int flags);
int link(const char *oldpath, const char *newpath);
int linkat(int old_directory, const char *oldpath, int new_directory,
           const char *newpath, int flags);
int symlink(const char *target, const char *linkpath);
ssize_t readlink(const char *path, char *buffer, size_t size);
int rmdir(const char *path);
int rename(const char *oldpath, const char *newpath);
int chown(const char *path, uid_t uid, gid_t gid);
int fchown(int fd, uid_t uid, gid_t gid);
#ifndef _DWM_C
int spawn(const char *path, char *const argv[]);
#endif
int execve(const char *path, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
int execvpe(const char *file, char *const argv[], char *const envp[]);
int execl(const char *path, const char *arg, ...);
int execlp(const char *file, const char *arg, ...);
int execle(const char *path, const char *arg, ...);
int pipe(int pipefd[2]);
int pipe2(int pipefd[2], int flags);
int kill(int pid, int signal);
int brk(void *addr);
void *sbrk(intptr_t increment);
int ftruncate(int descriptor, off_t length);
int ftruncate64(int descriptor, off_t length);
int truncate(const char *path, off_t length);
int truncate64(const char *path, off_t length);
int sync(void);
int fsync(int descriptor);
int fdatasync(int descriptor);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int getgroups(int size, gid_t list[]);
extern char *optarg;
extern int optind, opterr, optopt;
int getopt(int argc, char * const argv[], const char *optstring);
int setuid(uid_t uid);
int setgid(gid_t gid);
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgid(pid_t pid);
pid_t getpgrp(void);
pid_t getsid(pid_t pid);
pid_t setsid(void);
int tcsetpgrp(int descriptor, pid_t pgrp);
pid_t tcgetpgrp(int descriptor);
int posix_openpt(int flags);
int grantpt(int descriptor);
int unlockpt(int descriptor);
char *ptsname(int descriptor);
char *ttyname(int descriptor);
int isatty(int descriptor);
pid_t getpid(void);
pid_t getppid(void);
pid_t fork(void);
pid_t vfork(void);
int tty_set_echo(int enabled);
unsigned int sleep(unsigned int seconds);
int usleep(useconds_t usec);
unsigned int alarm(unsigned int seconds);
int daemon(int nochdir, int noclose);
_Noreturn void _exit(int status);

#ifdef __cplusplus
}
#endif

#endif
