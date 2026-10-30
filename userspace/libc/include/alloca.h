#ifndef AKAOS_LIBC_ALLOCA_H
#define AKAOS_LIBC_ALLOCA_H

#include <stddef.h>

/* alloca is a compiler-supported Unix extension rather than an external
 * allocator symbol. Keep upstream POSIX source builds within the akaOS
 * headers instead of falling through to the build host's libc headers. */
#if defined(__GNUC__) || defined(__clang__)
#define alloca(size) __builtin_alloca(size)
#else
void *alloca(size_t size);
#endif

#endif
