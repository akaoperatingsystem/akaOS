#ifndef XSCORE_MEMORY_H
#define XSCORE_MEMORY_H

#include <akaos/boot_info.h>
#include <stdint.h>
#include <stddef.h>

#define PMM_MAX_ORDER 10U       /* Orders 0..10: 4KB up to 4MB contiguous physical pages */

enum xscore_memory_zone {
    ZONE_DMA = 0,               /* 0 - 16 MB (Legacy ISA DMA) */
    ZONE_DMA32,                 /* 16 MB - 4 GB (32-bit PCI DMA) */
    ZONE_NORMAL,                /* 4 GB+ (Direct kernel mapped physical RAM) */
    ZONE_HIGHMEM,               /* Memory above 896 MB on 32-bit platforms */
    ZONE_MAX
};

struct xscore_physical_memory_stats {
    uint64_t managed_pages;
    uint64_t free_pages;
    uint64_t highest_address;
    uint64_t zone_pages[ZONE_MAX];
    uint64_t zone_free[ZONE_MAX];
};

struct xscore_smbios_memory_module {
    char device_locator[16];    /* e.g. "DIMM 0", "Channel A DIMM 1" */
    char bank_locator[16];
    char manufacturer[32];
    char serial[32];
    uint32_t size_mb;
    uint32_t speed_mhz;
    uint8_t form_factor;        /* DIMM (0x09), SODIMM (0x0E) */
    uint8_t memory_type;        /* DDR3 (0x18), DDR4 (0x1A), DDR5 (0x22), LPDDR4/5 */
    uint8_t rank;
    uint8_t present;
};

struct xscore_smbios_memory_info {
    uint32_t total_capacity_mb;
    uint32_t slot_count;
    uint8_t ecc_type;           /* None (0x03), Single-bit (0x05), Multi-bit (0x06) */
    struct xscore_smbios_memory_module modules[16];
};

int xscore_pmm_initialize(const struct akaos_boot_info *boot_info,
                          struct xscore_physical_memory_stats *stats);
int xscore_pmm_init_direct(uint64_t memory_bytes, uint64_t reserved_end);

/* Page Frame Allocator */
uint64_t xscore_pmm_allocate_page(void);
int xscore_pmm_free_page(uint64_t physical_address);

/* Multi-Page Order Buddy Allocator (Order 0..10: 4KB up to 4MB contiguous) */
uint64_t xscore_pmm_allocate_pages(uint32_t order);
int xscore_pmm_free_pages(uint64_t physical_address, uint32_t order);
uint64_t xscore_pmm_allocate_zone(enum xscore_memory_zone zone, uint32_t order);

/* Page Frame Statistics & Refcounts */
uint64_t xscore_pmm_free_page_count(void);
uint64_t xscore_pmm_total_page_count(void);
void xscore_pmm_refcount_inc(uint64_t physical_address);
int xscore_pmm_refcount_dec(uint64_t physical_address);
uint16_t xscore_pmm_refcount_get(uint64_t physical_address);

/* SMBIOS / DMI Hardware Memory Enumeration */
int xscore_smbios_parse_memory_topology(struct xscore_smbios_memory_info *info);
const struct xscore_smbios_memory_info *xscore_smbios_get_memory_info(void);

#endif /* XSCORE_MEMORY_H */
