#ifndef AKAOS_LIBC_DLFCN_H
#define AKAOS_LIBC_DLFCN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Small POSIX-compatible dynamic-loader interface.  XSCore loads
 * self-contained ET_DYN objects through this API; it intentionally keeps the
 * familiar dlopen/dlsym spelling so portable applications can be rebuilt. */
#define RTLD_LAZY  0x0001
#define RTLD_NOW   0x0002
#define RTLD_NOLOAD 0x0004
#define RTLD_LOCAL 0x0000
#define RTLD_GLOBAL 0x0100

/* Standard lookup scopes.  RTLD_DEFAULT is the process-wide global scope
 * (represented by a null handle); RTLD_NEXT starts after the DSO containing
 * the dlsym caller.  Keeping these values ABI-compatible lets GLib, GIO and
 * Qt plugins use their normal feature-probing paths. */
#define RTLD_DEFAULT ((void *)(uintptr_t)0)
#define RTLD_NEXT    ((void *)(uintptr_t)(-1))

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

void *dlopen(const char *path, int mode);
int dladdr(const void *address, Dl_info *information);
void *dlsym(void *handle, const char *name);
/* Resolve a symbol from a specific ELF symbol-version definition. */
void *dlvsym(void *handle, const char *name, const char *version);
/* akaOS native interpreter extension: return the relocated ET_DYN entry. */
void *akaos_dl_entry(void *handle);
/* Bounded native TLS address lookup used by the akaOS __tls_get_addr ABI. */
void *akaos_dl_tls_address(uintptr_t module, uintptr_t offset);
/* Install/refresh the ABI static-TLS area for the calling native thread. */
uintptr_t akaos_dl_static_tls_prepare(void);
void akaos_dl_static_tls_release(void);
/* Reclaim per-thread dynamic-loader error state at pthread exit. */
void akaos_dl_thread_cleanup(uint32_t thread);
/* Initialize main-executable PT_TLS before application code starts. */
int akaos_dl_main_tls_initialize(const char *path,
                                uintptr_t program_headers,
                                uintptr_t program_header_count,
                                uintptr_t program_header_entry_size);
int dlclose(void *handle);
char *dlerror(void);

#ifdef __cplusplus
}
#endif

#endif
