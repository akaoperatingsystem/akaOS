#include <xscore/memory.h>
#include <xscore/vmm.h>
#include <stdint.h>

#define PAGE_SIZE UINT64_C(4096)
#define ARM32_SECTION_SIZE UINT32_C(0x00100000)
#define ARM32_RAM_START UINT32_C(0x40000000)
#define ARM32_RAM_END UINT32_C(0x48000000)
#define ARM32_UART_BASE UINT32_C(0x09000000)
#define ARM32_L1_ENTRIES 4096U
#define ARM32_L1_ALIGNMENT UINT32_C(0x4000)
#define ARM32_USER_L1_INDEX_START UINT32_C(0x100)
#define ARM32_USER_L1_INDEX_END UINT32_C(0x110)

#define ARM32_DESC_FAULT UINT32_C(0x0)
#define ARM32_DESC_COARSE UINT32_C(0x1)
#define ARM32_DESC_SECTION UINT32_C(0x2)
#define ARM32_DESC_SMALL UINT32_C(0x2)
#define ARM32_DESC_SMALL_XN UINT32_C(0x3)
#define ARM32_DOMAIN0 UINT32_C(0)
#define ARM32_SECTION_AP_PRIV_RW (UINT32_C(1) << 10)
#define ARM32_SECTION_AP_USER_RW (UINT32_C(3) << 10)
#define ARM32_SECTION_AP2 (UINT32_C(1) << 15)
#define ARM32_SECTION_XN (UINT32_C(1) << 4)
#define ARM32_PAGE_AP_PRIV_RW (UINT32_C(1) << 4)
#define ARM32_PAGE_AP_USER_RW (UINT32_C(3) << 4)
#define ARM32_PAGE_AP2 (UINT32_C(1) << 9)
#define ARM32_CACHEABLE ((UINT32_C(1) << 3) | (UINT32_C(1) << 2))

static uint32_t kernel_root;
static uint32_t current_root;

extern void xscore_arm32_load_ttbr(uint32_t root);
extern void xscore_arm32_enable_mmu(void);
extern void xscore_arm32_invalidate_tlb(void);
extern void xscore_arm32_invalidate_page(uint32_t virtual_address);

static uint32_t *table_ptr(uint32_t physical) {
    return (uint32_t *)(uintptr_t)physical;
}

static void zero_words(uint32_t *table, uint32_t count) {
    for (uint32_t index = 0; index < count; ++index) table[index] = 0;
}

/* TTBR0 points at a 16 KiB-aligned 4096-entry first-level table. */
static uint32_t allocate_l1_table(void) {
    /* The PMM hands out 4 KiB pages. Consume alignment padding until the
     * first page satisfies ARMv7's 16 KiB TTBR0 requirement, then take the
     * following three contiguous pages for the 4096-entry table. */
    for (;;) {
        uint64_t first = xscore_pmm_allocate_page();
        if (!first) return 0;
        if ((first & (ARM32_L1_ALIGNMENT - 1)) != 0) continue;

        uint64_t pages[4] = {first, 0, 0, 0};
        int contiguous = 1;
        for (uint32_t index = 1; index < 4; ++index) {
            pages[index] = xscore_pmm_allocate_page();
            if (!pages[index] || pages[index] != first + index * PAGE_SIZE) {
                contiguous = 0;
            }
        }
        if (contiguous) {
            zero_words(table_ptr((uint32_t)first), ARM32_L1_ENTRIES);
            return (uint32_t)first;
        }
        for (uint32_t index = 1; index < 4; ++index) {
            if (pages[index]) xscore_pmm_free_page(pages[index]);
        }
    }
}

static void free_l1_table(uint32_t root) {
    for (uint32_t index = 0; index < 4; ++index) {
        xscore_pmm_free_page((uint64_t)root + index * PAGE_SIZE);
    }
}

static uint32_t section_descriptor(uint32_t physical, uint64_t flags) {
    uint32_t descriptor = (physical & 0xfff00000U) | ARM32_DESC_SECTION |
                          ARM32_DOMAIN0 << 5 | ARM32_SECTION_AP_PRIV_RW |
                          ARM32_CACHEABLE;
    if (flags & XSCORE_PAGE_USER) {
        descriptor = (descriptor & ~(UINT32_C(7) << 10)) |
                     ARM32_SECTION_AP_USER_RW;
        if (!(flags & XSCORE_PAGE_WRITABLE)) descriptor |= ARM32_SECTION_AP2;
    }
    if (flags & XSCORE_PAGE_NO_EXECUTE) descriptor |= ARM32_SECTION_XN;
    return descriptor;
}

static uint32_t page_descriptor(uint32_t physical, uint64_t flags) {
    uint32_t descriptor = (physical & 0xfffff000U) |
                          ((flags & XSCORE_PAGE_NO_EXECUTE) ?
                           ARM32_DESC_SMALL_XN : ARM32_DESC_SMALL) |
                          ARM32_CACHEABLE;
    descriptor |= (flags & XSCORE_PAGE_USER) ? ARM32_PAGE_AP_USER_RW
                                             : ARM32_PAGE_AP_PRIV_RW;
    if ((flags & XSCORE_PAGE_USER) && !(flags & XSCORE_PAGE_WRITABLE)) {
        descriptor |= ARM32_PAGE_AP2;
    }
    return descriptor;
}

static uint32_t *ensure_page_table(uint32_t *root, uint32_t l1_index,
                                   uint64_t flags) {
    uint32_t entry = root[l1_index];
    if (entry && (entry & 3U) != ARM32_DESC_COARSE) return 0;
    if (!entry) {
        uint64_t physical = xscore_pmm_allocate_page();
        if (!physical) return 0;
        uint32_t *table = table_ptr((uint32_t)physical);
        zero_words(table, 1024);
        root[l1_index] = (uint32_t)physical | ARM32_DESC_COARSE |
                          (ARM32_DOMAIN0 << 5);
        (void)flags;
        return table;
    }
    return table_ptr(entry & ~0xfffU);
}

int xscore_vmm_map_page_in(uint64_t root_address, uint64_t virtual_address,
                           uint64_t physical_address, uint64_t flags) {
    if (!root_address || virtual_address > UINT32_MAX ||
        physical_address > UINT32_MAX) return 0;
    uint32_t *root = table_ptr((uint32_t)root_address);
    uint32_t va = (uint32_t)virtual_address;
    uint32_t l1_index = va >> 20;
    uint32_t *table = ensure_page_table(root, l1_index, flags);
    if (!table) return 0;
    table[(va >> 12) & 0xffU] = page_descriptor((uint32_t)physical_address, flags);
    if ((uint32_t)root_address == current_root) xscore_arm32_invalidate_page(va);
    return 1;
}

int xscore_vmm_unmap_page_in(uint64_t root_address, uint64_t virtual_address,
                             uint64_t *physical_address) {
    if (!root_address || virtual_address > UINT32_MAX) return 0;
    uint32_t *root = table_ptr((uint32_t)root_address);
    uint32_t va = (uint32_t)virtual_address;
    uint32_t l1_entry = root[va >> 20];
    if (!l1_entry || (l1_entry & 3U) != ARM32_DESC_COARSE) return 0;
    uint32_t *table = table_ptr(l1_entry & ~0xfffU);
    uint32_t *pte = &table[(va >> 12) & 0xffU];
    if ((*pte & 3U) == ARM32_DESC_FAULT) return 0;
    if (physical_address) *physical_address = (uint64_t)(*pte & 0xfffff000U);
    *pte = 0;
    if ((uint32_t)root_address == current_root) xscore_arm32_invalidate_page(va);
    return 1;
}

int xscore_vmm_is_mapped_in(uint64_t root_address, uint64_t virtual_address,
                            uint64_t *flags, uint64_t *physical_address) {
    if (!root_address || virtual_address > UINT32_MAX) return 0;
    uint32_t *root = table_ptr((uint32_t)root_address);
    uint32_t va = (uint32_t)virtual_address;
    uint32_t l1_entry = root[va >> 20];
    if (!l1_entry) return 0;
    uint32_t descriptor = 0;
    int is_section = 0;
    if ((l1_entry & 3U) == ARM32_DESC_SECTION) {
        descriptor = l1_entry;
        is_section = 1;
        if (physical_address) {
            *physical_address = (uint64_t)(l1_entry & 0xfff00000U) +
                                 (va & 0xfffffU);
        }
    } else if ((l1_entry & 3U) == ARM32_DESC_COARSE) {
        uint32_t *table = table_ptr(l1_entry & ~0xfffU);
        descriptor = table[(va >> 12) & 0xffU];
        if ((descriptor & 3U) == ARM32_DESC_FAULT) return 0;
        if (physical_address) *physical_address = descriptor & 0xfffff000U;
    } else {
        return 0;
    }
    if (flags) {
        uint64_t result = XSCORE_PAGE_PRESENT;
        uint32_t ap = (descriptor >> 4) & 3U;
        uint32_t ap2 = (descriptor >> 9) & 1U;
        if (is_section) {
            ap = (descriptor >> 10) & 3U;
            ap2 = (descriptor >> 15) & 1U;
        }
        if (!ap2 && (ap == 1U || ap == 3U)) result |= XSCORE_PAGE_WRITABLE;
        if (ap == 3U) result |= XSCORE_PAGE_USER;
        if ((is_section && (descriptor & ARM32_SECTION_XN)) ||
            (!is_section && (descriptor & 1U))) {
            result |= XSCORE_PAGE_NO_EXECUTE;
        }
        *flags = result;
    }
    return 1;
}

int xscore_vmm_is_mapped(uint64_t virtual_address, uint64_t *flags) {
    return xscore_vmm_is_mapped_in(current_root, virtual_address, flags, 0);
}

int xscore_vmm_map_page(uint64_t virtual_address, uint64_t physical_address,
                        uint64_t flags) {
    return xscore_vmm_map_page_in(current_root, virtual_address,
                                  physical_address, flags);
}

int xscore_vmm_unmap_page(uint64_t virtual_address, uint64_t *physical_address) {
    return xscore_vmm_unmap_page_in(current_root, virtual_address, physical_address);
}

int xscore_vmm_update_page_flags(uint64_t virtual_address, uint64_t flags) {
    uint64_t physical = 0;
    if (!xscore_vmm_unmap_page(virtual_address, &physical)) return 0;
    return xscore_vmm_map_page(virtual_address, physical, flags);
}

int xscore_vmm_split_large_page(uint64_t virtual_address) {
    (void)virtual_address;
    return 1;
}

uint64_t xscore_vmm_root_table(void) { return kernel_root; }
uint64_t xscore_vmm_current_root(void) { return current_root; }

int xscore_vmm_activate(uint64_t root_address) {
    if (!root_address || root_address > UINT32_MAX) return 0;
    /* Kernel heap mappings can grow after a user address space is created.
     * Refresh the shared portion before installing TTBR0 so a context switch
     * never strands the scheduler on an unmapped kernel stack. */
    xscore_vmm_address_space_sync_kernel(root_address);
    current_root = (uint32_t)root_address;
    xscore_arm32_load_ttbr(current_root);
    xscore_arm32_invalidate_tlb();
    return 1;
}

uint64_t xscore_vmm_address_space_create(void) {
    uint32_t root = allocate_l1_table();
    if (!root) return 0;
    uint32_t *new_root = table_ptr(root);
    uint32_t *kernel = table_ptr(kernel_root);
    for (uint32_t index = 0; index < ARM32_L1_ENTRIES; ++index) {
        if (index >= ARM32_USER_L1_INDEX_START &&
            index < ARM32_USER_L1_INDEX_END) {
            new_root[index] = 0;
        } else {
            new_root[index] = kernel[index];
        }
    }
    return root;
}

void xscore_vmm_address_space_sync_kernel(uint64_t root_address) {
    if (!root_address || root_address == kernel_root) return;
    uint32_t *root = table_ptr((uint32_t)root_address);
    uint32_t *kernel = table_ptr(kernel_root);
    for (uint32_t index = 0; index < ARM32_L1_ENTRIES; ++index) {
        /* 0x10000000..0x10ffffff is the private process address window. */
        if (index >= ARM32_USER_L1_INDEX_START &&
            index < ARM32_USER_L1_INDEX_END) continue;
        root[index] = kernel[index];
    }
}

void xscore_vmm_address_space_destroy(uint64_t root_address) {
    if (!root_address || root_address == kernel_root) return;
    uint32_t *root = table_ptr((uint32_t)root_address);
    for (uint32_t index = ARM32_USER_L1_INDEX_START;
         index < ARM32_USER_L1_INDEX_END; ++index) {
        uint32_t entry = root[index];
        if (!entry || (entry & 3U) != ARM32_DESC_COARSE) continue;
        xscore_pmm_free_page(entry & ~0xfffU);
        root[index] = 0;
    }
    free_l1_table((uint32_t)root_address);
}

void xscore_vmm_arm32_init(void) {
    kernel_root = allocate_l1_table();
    if (!kernel_root) return;
    uint32_t *root = table_ptr(kernel_root);
    for (uint32_t address = ARM32_RAM_START; address < ARM32_RAM_END;
         address += ARM32_SECTION_SIZE) {
        root[address >> 20] = section_descriptor(address, XSCORE_PAGE_WRITABLE);
    }
    root[ARM32_UART_BASE >> 20] = section_descriptor(ARM32_UART_BASE,
                                                      XSCORE_PAGE_WRITABLE);
    root[0x0a000000 >> 20] = section_descriptor(0x0a000000, XSCORE_PAGE_WRITABLE);
    root[0x3f000000 >> 20] = section_descriptor(0x3f000000, XSCORE_PAGE_WRITABLE);
    current_root = kernel_root;
    xscore_arm32_load_ttbr(kernel_root);
    xscore_arm32_enable_mmu();
    xscore_arm32_invalidate_tlb();
}
