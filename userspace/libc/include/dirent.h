#ifndef AKAOS_LIBC_DIRENT_H
#define AKAOS_LIBC_DIRENT_H

#include <akaos/fs.h>
#include <stddef.h>
#include <sys/types.h>

#ifndef NAME_MAX
#define NAME_MAX AKAOS_NAME_MAX
#endif
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

struct dirent {
    ino_t d_ino;
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[NAME_MAX + 1];
};

typedef struct __akaos_DIR DIR;

#ifdef __cplusplus
extern "C" {
#endif

ssize_t getdents(int descriptor, struct akaos_dirent *entries,
                 size_t capacity);
DIR *opendir(const char *path);
DIR *fdopendir(int descriptor);
struct dirent *readdir(DIR *directory);
int closedir(DIR *directory);
void rewinddir(DIR *directory);
long telldir(DIR *directory);
void seekdir(DIR *directory, long location);
int dirfd(DIR *directory);

#ifdef __cplusplus
}
#endif

#endif
