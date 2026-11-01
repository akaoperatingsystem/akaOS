#ifndef XSCORE_MEMORY_H
#define XSCORE_MEMORY_H

#include <akaos/boot_info.h>
#include <stdint.h>

struct xscore_physical_memory_stats {
    uint64_t managed_pages;
    uint64_t free_pages;
    uint64_t highest_address;
};

int xscore_pmm_initialize(const struct akaos_boot_info *boot_info,
                          struct xscore_physical_memory_stats *stats);
int xscore_pmm_init_direct(uint64_t memory_bytes, uint64_t reserved_end);
uint64_t xscore_pmm_allocate_page(void);
int xscore_pmm_free_page(uint64_t physical_address);
uint64_t xscore_pmm_free_page_count(void);
uint64_t xscore_pmm_total_page_count(void);
void xscore_pmm_refcount_inc(uint64_t physical_address);
int xscore_pmm_refcount_dec(uint64_t physical_address);
uint16_t xscore_pmm_refcount_get(uint64_t physical_address);

#endif
