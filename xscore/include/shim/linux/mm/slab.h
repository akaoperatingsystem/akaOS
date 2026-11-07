#ifndef _SHIM_LINUX_MM_SLAB_H
#define _SHIM_LINUX_MM_SLAB_H

#include <shim/linux/types.h>

void *kmalloc(size_t size, gfp_t flags);
void kfree(const void *ptr);

#endif /* _SHIM_LINUX_MM_SLAB_H */
