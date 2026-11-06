#ifndef _SYS_STATFS_H
#define _SYS_STATFS_H

#include <features.h>
#include <sys/types.h>

typedef struct { int val[2]; } fsid_t;

struct statfs {
    unsigned long f_type;
    unsigned long f_bsize;
    fsblkcnt_t    f_blocks;
    fsblkcnt_t    f_bfree;
    fsblkcnt_t    f_bavail;
    fsfilcnt_t    f_files;
    fsfilcnt_t    f_ffree;
    fsid_t        f_fsid;
    unsigned long f_namelen;
    unsigned long f_frsize;
    unsigned long f_flags;
    unsigned long f_spare[4];
};

#ifdef __cplusplus
extern "C" {
#endif

int statfs(const char *path, struct statfs *buf);
int fstatfs(int fd, struct statfs *buf);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_STATFS_H */
