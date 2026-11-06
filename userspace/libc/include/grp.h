#ifndef AKAOS_LIBC_GRP_H
#define AKAOS_LIBC_GRP_H

#include <features.h>
#include <stddef.h>
#include <sys/types.h>

struct group {
    char *gr_name;
    char *gr_passwd;
    gid_t gr_gid;
    char **gr_mem;
};

#ifdef __cplusplus
extern "C" {
#endif

struct group *getgrnam(const char *name);
struct group *getgrgid(gid_t gid);
int getgrnam_r(const char *name, struct group *entry, char *buffer,
               size_t buffer_size, struct group **result);
int getgrgid_r(gid_t gid, struct group *entry, char *buffer,
               size_t buffer_size, struct group **result);
void setgrent(void);
void endgrent(void);
struct group *getgrent(void);
int setgroups(size_t size, const gid_t *list);

#ifdef __cplusplus
}
#endif

#endif
