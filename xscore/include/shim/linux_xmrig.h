#ifndef _LINUX_XMRIG_SHIM_H
#define _LINUX_XMRIG_SHIM_H

#include <stddef.h>
#include <stdint.h>

/*
 * XSCore native compute engine interface definitions for XMRig.
 * High-performance lockless hugepages, zero-copy memory mappings,
 * thread affinity, and hardware performance counter discovery.
 */

#define XMRIG_SHIM_VERSION "1.0.0"

#ifndef MADV_HUGEPAGE
#define MADV_HUGEPAGE 14
#endif

#ifndef MADV_NOHUGEPAGE
#define MADV_NOHUGEPAGE 15
#endif

#ifndef MAP_HUGETLB
#define MAP_HUGETLB 0x40000
#endif

/* CPU affinity set structure for native thread scheduling binding */
#ifndef CPU_SETSIZE
#define CPU_SETSIZE 1024
typedef struct {
    uint64_t __bits[CPU_SETSIZE / 64];
} cpu_set_t;

#define CPU_ZERO(set) do {     for (size_t __i = 0; __i < (CPU_SETSIZE / 64); ++__i) (set)->__bits[__i] = 0; } while (0)

#define CPU_SET(cpu, set) do {     size_t __cpu = (size_t)(cpu);     if (__cpu < CPU_SETSIZE) (set)->__bits[__cpu / 64] |= (1ULL << (__cpu % 64)); } while (0)

#define CPU_CLR(cpu, set) do {     size_t __cpu = (size_t)(cpu);     if (__cpu < CPU_SETSIZE) (set)->__bits[__cpu / 64] &= ~(1ULL << (__cpu % 64)); } while (0)

#define CPU_ISSET(cpu, set) ({     size_t __cpu = (size_t)(cpu);     (__cpu < CPU_SETSIZE) ? !!((set)->__bits[__cpu / 64] & (1ULL << (__cpu % 64))) : 0; })
#endif

#endif /* _LINUX_XMRIG_SHIM_H */
