#ifndef XSCORE_CPU_H
#define XSCORE_CPU_H

#include <stdint.h>
#include <stddef.h>

struct xscore_cpu_info {
    char vendor[16];
    char brand[64];
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t type;
    uint32_t mhz;
    uint32_t bogomips_int;
    uint32_t bogomips_frac;
    uint32_t cache_size_kb;
    uint32_t cache_line_bytes;
    uint32_t phys_addr_bits;
    uint32_t virt_addr_bits;
    char flags[512];
};

void xscore_cpu_initialize(void);
const struct xscore_cpu_info *xscore_cpu_get_info(void);
size_t xscore_cpu_format_proc_cpuinfo(char *buf, size_t max_size);

#endif
