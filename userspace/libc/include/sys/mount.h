#ifndef _SYS_MOUNT_H
#define _SYS_MOUNT_H

#include <akaos/fs.h>

#define MS_RDONLY      AKAOS_MS_RDONLY
#define MS_NOSUID      AKAOS_MS_NOSUID
#define MS_NODEV       AKAOS_MS_NODEV
#define MS_NOEXEC      AKAOS_MS_NOEXEC
#define MS_SYNCHRONOUS AKAOS_MS_SYNCHRONOUS
#define MS_REMOUNT     AKAOS_MS_REMOUNT

int mount(const char *source, const char *target,
          const char *filesystemtype, unsigned long mountflags,
          const void *data);

int umount(const char *target);
int umount2(const char *target, int flags);

#endif
