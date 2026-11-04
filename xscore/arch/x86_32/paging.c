#include <xscore/vmm.h>
#include <xscore/memory.h>
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096U
#define PAGE_PRESENT  0x01U
#define PAGE_WRITABLE 0x02U
#define PAGE_USER     0x04U

#define KERNEL_IDENTITY_TABLES 64U  /* 256 MB identity mapped at boot */
#define KERNEL_SHARED_TABLES   256U /* 1 GB kernel space (0x00000000 - 0x3FFFFFFF) */

static uint32_t __attribute__((aligned(4096))) initial_page_directory[1024];
static uint32_t __attribute__((aligned(4096))) initial_kernel_tables[KERNEL_IDENTITY_TABLES][1024];
static uint32_t current_cr3_root = 0;

extern void xscore_32_load_cr3(uint32_t cr3);
extern void xscore_32_enable_paging(void);
extern void xscore_32_invlpg(uint32_t vaddr);

void xscore_32_paging_init(void) {
    for (int i = 0; i < 1024; ++i) {
        initial_page_directory[i] = 0;
    }

    /* Identity map first 256 MB of physical memory */
    for (uint32_t t = 0; t < KERNEL_IDENTITY_TABLES; ++t) {
        for (int i = 0; i < 1024; ++i) {
            uint32_t paddr = (uint32_t)((t * 1024 + i) * PAGE_SIZE);
            if (t == 0 && i == 0) {
                initial_kernel_tables[t][i] = 0; /* Null page guard: unmapped */
            } else {
                initial_kernel_tables[t][i] = paddr | PAGE_PRESENT | PAGE_WRITABLE;
            }
        }
        initial_page_directory[t] = ((uint32_t)&initial_kernel_tables[t][0]) | PAGE_PRESENT | PAGE_WRITABLE;
    }

    current_cr3_root = (uint32_t)&initial_page_directory[0];
    xscore_32_load_cr3(current_cr3_root);
    xscore_32_enable_paging();
}

int xscore_vmm_initialize(const struct akaos_boot_info *boot_info,
                          const struct xscore_physical_memory_stats *physical_stats,
                          struct xscore_virtual_memory_stats *virtual_stats) {
    (void)physical_stats;
    xscore_32_paging_init();

    /* Map framebuffer into initial page directory */
    if (boot_info && boot_info->framebuffer.address && boot_info->framebuffer.size) {
        uint64_t fb_start = boot_info->framebuffer.address;
        uint64_t fb_end = fb_start + boot_info->framebuffer.size;
        for (uint64_t addr = fb_start; addr < fb_end; addr += PAGE_SIZE) {
            xscore_vmm_map_page(addr, addr, XSCORE_PAGE_WRITABLE);
        }
    }

    if (virtual_stats) {
        virtual_stats->identity_mapped_bytes = (uint64_t)KERNEL_IDENTITY_TABLES * 1024 * PAGE_SIZE;
        virtual_stats->page_table_pages = KERNEL_IDENTITY_TABLES + 1;
        virtual_stats->root_table_address = (uint64_t)current_cr3_root;
        virtual_stats->null_page_guarded = 1;
    }
    return 1;
}

uint64_t xscore_vmm_root_table(void) {
    return (uint64_t)current_cr3_root;
}

uint64_t xscore_vmm_current_root(void) {
    return (uint64_t)current_cr3_root;
}

int xscore_vmm_activate(uint64_t root_address) {
    current_cr3_root = (uint32_t)root_address;
    xscore_32_load_cr3(current_cr3_root);
    return 1;
}

uint64_t xscore_vmm_address_space_create(void) {
    uint64_t pd_phys = xscore_pmm_allocate_page();
    if (!pd_phys) return 0;

    uint32_t *pd = (uint32_t*)(uintptr_t)pd_phys;
    for (int i = 0; i < 1024; ++i) {
        pd[i] = 0;
    }

    /* Share all kernel space and MMIO mappings across every process:
     * 1. All kernel tables < KERNEL_SHARED_TABLES (0x00000000 - 0x3FFFFFFF, including identity map & thread stacks)
     * 2. All MMIO tables >= 0xC0000000 (including framebuffer at 0xE0000000, APIC, PCI MMIO)
     */
    for (uint32_t t = 0; t < 1024; ++t) {
        if (initial_page_directory[t] != 0) {
            if (t < KERNEL_SHARED_TABLES || t >= 768) {
                pd[t] = initial_page_directory[t];
            }
        }
    }

    return pd_phys;
}

void xscore_vmm_address_space_sync_kernel(uint64_t root_address) {
    if (!root_address) return;
    uint32_t *pd = (uint32_t*)(uintptr_t)root_address;
    /* Share all kernel space and MMIO mappings across every process:
     * 1. All kernel tables < KERNEL_SHARED_TABLES (0x00000000 - 0x3FFFFFFF, including identity map & thread stacks)
     * 2. All MMIO tables >= 0xC0000000 (including framebuffer at 0xE0000000, APIC, PCI MMIO)
     */
    for (uint32_t t = 0; t < 1024; ++t) {
        if (initial_page_directory[t] != 0) {
            if (t < KERNEL_SHARED_TABLES || t >= 768) {
                pd[t] = initial_page_directory[t];
            }
        }
    }
}

void xscore_vmm_address_space_destroy(uint64_t root_address) {
    if (!root_address || root_address == (uint64_t)&initial_page_directory[0]) return;
    uint32_t *pd = (uint32_t*)(uintptr_t)root_address;

    /* Free any private user page tables allocated for this address space */
    for (uint32_t dir_idx = 0; dir_idx < 1024; ++dir_idx) {
        if ((pd[dir_idx] & PAGE_PRESENT) && (pd[dir_idx] & PAGE_USER)) {
            uint32_t pt_paddr = pd[dir_idx] & ~0xFFFU;
            xscore_pmm_free_page(pt_paddr);
            pd[dir_idx] = 0;
        }
    }
    xscore_pmm_free_page(root_address);
}

int xscore_vmm_map_page_in(uint64_t root_address, uint64_t virtual_address,
                           uint64_t physical_address, uint64_t flags) {
    if (!root_address || virtual_address == 0) return 0;
    uint32_t *pd = (uint32_t*)(uintptr_t)root_address;
    uint32_t vaddr32 = (uint32_t)virtual_address;
    uint32_t paddr32 = (uint32_t)physical_address;

    uint32_t dir_idx = vaddr32 >> 22;
    uint32_t tbl_idx = (vaddr32 >> 12) & 0x3FFU;

    if (!(pd[dir_idx] & PAGE_PRESENT)) {
        uint64_t pt_phys = xscore_pmm_allocate_page();
        if (!pt_phys) return 0;

        uint32_t *pt = (uint32_t*)(uintptr_t)pt_phys;
        for (int i = 0; i < 1024; ++i) pt[i] = 0;

        pd[dir_idx] = (uint32_t)pt_phys | PAGE_PRESENT | PAGE_WRITABLE |
                      ((flags & XSCORE_PAGE_USER) ? PAGE_USER : 0);
        if (!(flags & XSCORE_PAGE_USER)) {
            initial_page_directory[dir_idx] = pd[dir_idx];
        }
    } else {
        if (flags & XSCORE_PAGE_USER) {
            pd[dir_idx] |= PAGE_USER;
        }
        if (!(flags & XSCORE_PAGE_USER)) {
            initial_page_directory[dir_idx] = pd[dir_idx];
        }
    }

    uint32_t *table = (uint32_t*)(uintptr_t)(pd[dir_idx] & ~0xFFFU);
    uint32_t pte_flags = PAGE_PRESENT;
    if (flags & XSCORE_PAGE_WRITABLE) pte_flags |= PAGE_WRITABLE;
    if (flags & XSCORE_PAGE_USER) pte_flags |= PAGE_USER;

    table[tbl_idx] = (paddr32 & ~0xFFFU) | pte_flags;

    if ((uint32_t)root_address == current_cr3_root) {
        xscore_32_invlpg(vaddr32);
    }
    return 1;
}

int xscore_vmm_unmap_page_in(uint64_t root_address, uint64_t virtual_address,
                             uint64_t *physical_address) {
    if (!root_address) return 0;
    uint32_t *pd = (uint32_t*)(uintptr_t)root_address;
    uint32_t vaddr32 = (uint32_t)virtual_address;

    uint32_t dir_idx = vaddr32 >> 22;
    uint32_t tbl_idx = (vaddr32 >> 12) & 0x3FFU;

    if (!(pd[dir_idx] & PAGE_PRESENT)) return 0;

    uint32_t *table = (uint32_t*)(uintptr_t)(pd[dir_idx] & ~0xFFFU);
    if (!(table[tbl_idx] & PAGE_PRESENT)) return 0;

    if (physical_address) {
        *physical_address = (uint64_t)(table[tbl_idx] & ~0xFFFU);
    }
    table[tbl_idx] = 0;

    if ((uint32_t)root_address == current_cr3_root) {
        xscore_32_invlpg(vaddr32);
    }
    return 1;
}

int xscore_vmm_is_mapped_in(uint64_t root_address, uint64_t virtual_address,
                            uint64_t *flags, uint64_t *physical_address) {
    if (!root_address) return 0;
    uint32_t *pd = (uint32_t*)(uintptr_t)root_address;
    uint32_t vaddr32 = (uint32_t)virtual_address;

    uint32_t dir_idx = vaddr32 >> 22;
    uint32_t tbl_idx = (vaddr32 >> 12) & 0x3FFU;

    if (!(pd[dir_idx] & PAGE_PRESENT)) return 0;
    uint32_t *table = (uint32_t*)(uintptr_t)(pd[dir_idx] & ~0xFFFU);
    if (!(table[tbl_idx] & PAGE_PRESENT)) return 0;

    if (flags) {
        uint64_t f = XSCORE_PAGE_PRESENT;
        if (table[tbl_idx] & PAGE_WRITABLE) f |= XSCORE_PAGE_WRITABLE;
        if (table[tbl_idx] & PAGE_USER) f |= XSCORE_PAGE_USER;
        *flags = f;
    }
    if (physical_address) {
        *physical_address = (uint64_t)(table[tbl_idx] & ~0xFFFU);
    }
    return 1;
}

int xscore_vmm_is_mapped(uint64_t virtual_address, uint64_t *flags) {
    return xscore_vmm_is_mapped_in(current_cr3_root, virtual_address, flags, NULL);
}

int xscore_vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags) {
    return xscore_vmm_map_page_in(current_cr3_root, virtual_address, physical_address, flags);
}

int xscore_vmm_unmap_page(uint64_t virtual_address, uint64_t *physical_address) {
    return xscore_vmm_unmap_page_in(current_cr3_root, virtual_address, physical_address);
}

int xscore_vmm_update_page_flags(uint64_t virtual_address, uint64_t flags) {
    uint32_t *pd = (uint32_t*)(uintptr_t)current_cr3_root;
    uint32_t vaddr32 = (uint32_t)virtual_address;
    uint32_t dir_idx = vaddr32 >> 22;
    uint32_t tbl_idx = (vaddr32 >> 12) & 0x3FFU;

    if (!pd || !(pd[dir_idx] & PAGE_PRESENT)) return 0;
    uint32_t *table = (uint32_t*)(uintptr_t)(pd[dir_idx] & ~0xFFFU);
    if (!(table[tbl_idx] & PAGE_PRESENT)) return 0;

    uint32_t paddr32 = table[tbl_idx] & ~0xFFFU;
    uint32_t pte_flags = PAGE_PRESENT;
    if (flags & XSCORE_PAGE_WRITABLE) pte_flags |= PAGE_WRITABLE;
    if (flags & XSCORE_PAGE_USER) pte_flags |= PAGE_USER;

    table[tbl_idx] = paddr32 | pte_flags;
    xscore_32_invlpg(vaddr32);
    return 1;
}

int xscore_vmm_split_large_page(uint64_t virtual_address) {
    (void)virtual_address;
    return 1;
}
