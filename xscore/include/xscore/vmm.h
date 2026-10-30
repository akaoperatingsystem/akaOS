#ifndef XSCORE_VMM_H
#define XSCORE_VMM_H

#include <akaos/boot_info.h>
#include <xscore/memory.h>
#include <stdint.h>

#define XSCORE_PAGE_PRESENT UINT64_C(0x001)
#define XSCORE_PAGE_WRITABLE UINT64_C(0x002)
#define XSCORE_PAGE_USER UINT64_C(0x004)
#define XSCORE_PAGE_NO_EXECUTE (UINT64_C(1) << 63)

struct xscore_virtual_memory_stats {
    uint64_t identity_mapped_bytes;
    uint64_t page_table_pages;
    uint64_t root_table_address;
    uint32_t null_page_guarded;
};

int xscore_vmm_initialize(const struct akaos_boot_info *boot_info,
                          const struct xscore_physical_memory_stats *physical_stats,
                          struct xscore_virtual_memory_stats *virtual_stats);
int xscore_vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
int xscore_vmm_unmap_page(uint64_t virtual_address, uint64_t *physical_address);
int xscore_vmm_update_page_flags(uint64_t virtual_address, uint64_t flags);
int xscore_vmm_split_large_page(uint64_t virtual_address);
int xscore_vmm_is_mapped(uint64_t virtual_address, uint64_t *flags);
uint64_t xscore_vmm_root_table(void);

/* Per-process address spaces. Kernel mappings are shared; user branches
 * are private to each returned root table. */
uint64_t xscore_vmm_address_space_create(void);
void xscore_vmm_address_space_sync_kernel(uint64_t root_address);
void xscore_vmm_address_space_destroy(uint64_t root_address);
int xscore_vmm_activate(uint64_t root_address);
uint64_t xscore_vmm_current_root(void);
int xscore_vmm_map_page_in(uint64_t root_address, uint64_t virtual_address,
                           uint64_t physical_address, uint64_t flags);
int xscore_vmm_unmap_page_in(uint64_t root_address, uint64_t virtual_address,
                             uint64_t *physical_address);
int xscore_vmm_is_mapped_in(uint64_t root_address, uint64_t virtual_address,
                            uint64_t *flags, uint64_t *physical_address);

#endif
