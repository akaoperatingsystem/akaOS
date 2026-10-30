#ifndef AKAOS_FS_H
#define AKAOS_FS_H

#include <stdint.h>

#define AKAOS_NAME_MAX 255

#define AKAOS_DT_DIRECTORY 1
#define AKAOS_DT_REGULAR 2
#define AKAOS_DT_CHARACTER 3

#define AKAOS_MODE_DIRECTORY UINT32_C(0040000)
#define AKAOS_MODE_CHARACTER UINT32_C(0020000)
#define AKAOS_MODE_REGULAR UINT32_C(0100000)
#define AKAOS_MODE_ISUID UINT32_C(0004000)
#define AKAOS_MODE_ISGID UINT32_C(0002000)
#define AKAOS_MODE_ISVTX UINT32_C(0001000)
#define AKAOS_MODE_IRUSR UINT32_C(0000400)
#define AKAOS_MODE_IWUSR UINT32_C(0000200)
#define AKAOS_MODE_IXUSR UINT32_C(0000100)
#define AKAOS_MODE_IRGRP UINT32_C(0000040)
#define AKAOS_MODE_IWGRP UINT32_C(0000020)
#define AKAOS_MODE_IXGRP UINT32_C(0000010)
#define AKAOS_MODE_IROTH UINT32_C(0000004)
#define AKAOS_MODE_IWOTH UINT32_C(0000002)
#define AKAOS_MODE_IXOTH UINT32_C(0000001)
#define AKAOS_MODE_READ  UINT32_C(0000444)
#define AKAOS_MODE_WRITE UINT32_C(0000222)
#define AKAOS_MODE_EXECUTE UINT32_C(0000111)

#define AKAOS_O_RDONLY UINT64_C(0)
#define AKAOS_O_WRONLY UINT64_C(1)
#define AKAOS_O_RDWR UINT64_C(2)
#define AKAOS_O_CREAT UINT64_C(0x40)
#define AKAOS_O_EXCL UINT64_C(0x80)
#define AKAOS_O_TRUNC UINT64_C(0x200)
#define AKAOS_O_APPEND UINT64_C(0x400)
#define AKAOS_O_NONBLOCK UINT64_C(0x800)
#define AKAOS_O_CLOEXEC UINT64_C(0x80000)

struct akaos_stat {
    uint64_t inode;
    uint64_t size;
    uint32_t mode;
    uint32_t type;
    uint32_t uid;
    uint32_t gid;
};

struct akaos_dirent {
    uint64_t inode;
    uint32_t type;
    char name[AKAOS_NAME_MAX + 1];
};

#define AKAOS_MS_RDONLY   UINT64_C(1)
#define AKAOS_MS_NOSUID   UINT64_C(2)
#define AKAOS_MS_NODEV    UINT64_C(4)
#define AKAOS_MS_NOEXEC   UINT64_C(8)
#define AKAOS_MS_SYNCHRONOUS UINT64_C(16)
#define AKAOS_MS_REMOUNT  UINT64_C(32)

struct akaos_mount_args {
    const char *source;
    const char *target;
    const char *fstype;
    uint64_t flags;
    const void *data;
};

#endif
