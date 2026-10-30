#ifndef AKAOS_LIBC_SYS_UTSNAME_H
#define AKAOS_LIBC_SYS_UTSNAME_H

#define UTSNAME_FIELD_LENGTH 65

struct utsname {
    char sysname[UTSNAME_FIELD_LENGTH];
    char nodename[UTSNAME_FIELD_LENGTH];
    char release[UTSNAME_FIELD_LENGTH];
    char version[UTSNAME_FIELD_LENGTH];
    char machine[UTSNAME_FIELD_LENGTH];
};

#ifdef __cplusplus
extern "C" {
#endif
int uname(struct utsname *name);
#ifdef __cplusplus
}
#endif

#endif
