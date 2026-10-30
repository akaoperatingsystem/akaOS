#ifndef AKAOS_LIBC_SYS_MMAN_H
#define AKAOS_LIBC_SYS_MMAN_H

#include <akaos/mman.h>
#include <akaos/syscall.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define PROT_NONE  AKAOS_PROT_NONE
#define PROT_READ  AKAOS_PROT_READ
#define PROT_WRITE AKAOS_PROT_WRITE
#define PROT_EXEC  AKAOS_PROT_EXEC

#define MAP_PRIVATE   AKAOS_MAP_PRIVATE
#define MAP_SHARED    AKAOS_MAP_SHARED
#define MAP_FIXED     AKAOS_MAP_FIXED
#define MAP_ANONYMOUS AKAOS_MAP_ANONYMOUS
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED ((void *)(intptr_t)-1)
#define MS_ASYNC 0x01
#define MS_INVALIDATE 0x02
#define MS_SYNC 0x04
#define MFD_CLOEXEC AKAOS_MFD_CLOEXEC
#define MFD_ALLOW_SEALING AKAOS_MFD_ALLOW_SEALING

#ifdef __cplusplus
extern "C" {
#endif
void *mmap(void *address, size_t length, int protection, int flags,
           int descriptor, int64_t offset);
int munmap(void *address, size_t length);
int mprotect(void *address, size_t length, int protection);
int msync(void *address, size_t length, int flags);
int memfd_create(const char *name, unsigned int flags);
int shm_open(const char *name, int flags, mode_t mode);
int shm_unlink(const char *name);
#ifdef __cplusplus
}
#endif

#endif
