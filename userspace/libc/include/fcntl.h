#ifndef AKAOS_LIBC_FCNTL_H
#define AKAOS_LIBC_FCNTL_H

#include <akaos/syscall.h>
#include <sys/types.h>

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_EXCL   0x80
#define O_TRUNC  0x200
#define O_APPEND 0x400
#define O_NONBLOCK 0x800
#define O_DIRECTORY 0x10000
#define O_NOFOLLOW 0x20000
#define O_CLOEXEC 0x80000

#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7
#define F_ADD_SEALS AKAOS_F_ADD_SEALS
#define F_GET_SEALS AKAOS_F_GET_SEALS
#define F_DUPFD_CLOEXEC 1030
#define FD_CLOEXEC 1

#define F_SEAL_SEAL AKAOS_F_SEAL_SEAL
#define F_SEAL_SHRINK AKAOS_F_SEAL_SHRINK
#define F_SEAL_GROW AKAOS_F_SEAL_GROW
#define F_SEAL_WRITE AKAOS_F_SEAL_WRITE
#define F_SEAL_FUTURE_WRITE AKAOS_F_SEAL_FUTURE_WRITE

#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2

struct flock {
    short l_type;
    short l_whence;
    off_t l_start;
    off_t l_len;
    pid_t l_pid;
};

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif
#ifndef AT_SYMLINK_FOLLOW
#define AT_SYMLINK_FOLLOW AKAOS_AT_SYMLINK_FOLLOW
#endif

#ifdef __cplusplus
extern "C" {
#endif

int open(const char *path, int flags, ...);
int openat(int directory, const char *path, int flags, ...);
int creat(const char *path, mode_t mode);
int fcntl(int descriptor, int command, ...);

#ifdef __cplusplus
}
#endif

#endif
