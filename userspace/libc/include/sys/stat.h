#ifndef AKAOS_LIBC_SYS_STAT_H
#define AKAOS_LIBC_SYS_STAT_H

#include <akaos/fs.h>
#include <sys/types.h>
#include <time.h>

#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFIFO  0010000
#define S_IFREG  0100000
#define S_IFLNK  0120000
#define S_IFSOCK 0140000

#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000
#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif

#define S_IRWXU 00700
#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100

#define S_IRWXG 00070
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010

#define S_IRWXO 00007
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
};

#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec
#ifndef PATH_MAX
#define PATH_MAX 128
#endif

#ifdef __cplusplus
extern "C" {
#endif
int stat(const char *path, struct stat *status);
int stat64(const char *path, struct stat *status);
int lstat(const char *path, struct stat *status);
int lstat64(const char *path, struct stat *status);
int fstat(int descriptor, struct stat *status);
int fstat64(int descriptor, struct stat *status);
int fstatat(int directory, const char *path, struct stat *status, int flags);
int fstatat64(int directory, const char *path, struct stat *status, int flags);
int akaos_stat(const char *path, struct akaos_stat *status);
int mkdir(const char *path, mode_t mode);
int mkdirat(int directory, const char *path, mode_t mode);
int mkfifo(const char *path, mode_t mode);
mode_t umask(mode_t mask);
int chmod(const char *path, mode_t mode);
int fchmod(int descriptor, mode_t mode);
int fchmodat(int directory, const char *path, mode_t mode, int flags);
#ifdef __cplusplus
}
#else
#ifndef AKAOS_LIBC_SYS_STAT_IMPLEMENTATION
/* Existing akaOS C utilities use the compact native stat ABI. Standard C++
 * and POSIX code use struct stat and the translated standard stat function. */
#define stat(path, status) \
    _Generic((status), struct akaos_stat *: akaos_stat, \
             const struct akaos_stat *: akaos_stat, \
             default: stat)((path), (status))
#endif
#endif

#endif
