#include <xscore/vmm.h>
#include <xscore/memory.h>
#include <stdint.h>
#include <stddef.h>

#define MMU_DESC_TABLE   0x3ULL
#define MMU_DESC_PAGE    0x3ULL
#define MMU_ATTR_NORMAL  (0ULL << 2)
#define MMU_ATTR_DEVICE  (1ULL << 2)
#define MMU_AP_RW_EL1    (0ULL << 6)
#define MMU_AP_RW_EL0    (1ULL << 6)
#define MMU_ACCESS_FLAG  (1ULL << 10)
#define MMU_INNER_SHARE  (3ULL << 8)

static uint64_t kernel_root_ttbr = 0;
static uint64_t current_ttbr0 = 0;

static inline void aarch64_tlbi_all(void) {
    __asm__ volatile("tlbi vmalle1is\n dsb ish\n isb" ::: "memory");
}

static inline void aarch64_tlbi_va(uint64_t va) {
    __asm__ volatile("tlbi vaae1is, %0\n dsb ish\n isb" :: "r"(va >> 12) : "memory");
}

int xscore_vmm_map_page_in(uint64_t root, uint64_t va, uint64_t pa, uint64_t flags) {
    if (va >= 0xFFFF000000000000ULL) {
        root = kernel_root_ttbr;
    }
    if (!root) return 0;
    uint64_t *l0 = (uint64_t*)(uintptr_t)root;

    uint64_t l0_idx = (va >> 39) & 0x1FF;
    uint64_t l1_idx = (va >> 30) & 0x1FF;
    uint64_t l2_idx = (va >> 21) & 0x1FF;
    uint64_t l3_idx = (va >> 12) & 0x1FF;

    if (!(l0[l0_idx] & 1)) {
        uint64_t l1_phys = xscore_pmm_allocate_page();
        if (!l1_phys) return 0;
        uint64_t *l1_tbl = (uint64_t*)(uintptr_t)l1_phys;
        for (int i = 0; i < 512; ++i) l1_tbl[i] = 0;
        l0[l0_idx] = l1_phys | MMU_DESC_TABLE;
    }
    uint64_t *l1 = (uint64_t*)(uintptr_t)(l0[l0_idx] & ~0xFFFULL);

    if (!(l1[l1_idx] & 1)) {
        uint64_t l2_phys = xscore_pmm_allocate_page();
        if (!l2_phys) return 0;
        uint64_t *l2_tbl = (uint64_t*)(uintptr_t)l2_phys;
        for (int i = 0; i < 512; ++i) l2_tbl[i] = 0;
        l1[l1_idx] = l2_phys | MMU_DESC_TABLE;
    }
    uint64_t *l2 = (uint64_t*)(uintptr_t)(l1[l1_idx] & ~0xFFFULL);

    if (!(l2[l2_idx] & 1)) {
        uint64_t l3_phys = xscore_pmm_allocate_page();
        if (!l3_phys) return 0;
        uint64_t *l3_tbl = (uint64_t*)(uintptr_t)l3_phys;
        for (int i = 0; i < 512; ++i) l3_tbl[i] = 0;
        l2[l2_idx] = l3_phys | MMU_DESC_TABLE;
    }
    uint64_t *l3 = (uint64_t*)(uintptr_t)(l2[l2_idx] & ~0xFFFULL);

    uint64_t desc = (pa & ~0xFFFULL) | MMU_DESC_PAGE | MMU_ACCESS_FLAG;
    if (pa < 0x40000000ULL) {
        desc |= MMU_ATTR_DEVICE;
    } else {
        desc |= MMU_ATTR_NORMAL | MMU_INNER_SHARE;
    }
    if (flags & XSCORE_PAGE_USER) {
        desc |= MMU_AP_RW_EL0;
    } else {
        desc |= MMU_AP_RW_EL1;
    }
    if (!(flags & XSCORE_PAGE_WRITABLE)) {
        desc |= (1ULL << 7);
    }
    if (flags & XSCORE_PAGE_NO_EXECUTE) {
        desc |= (1ULL << 54);
    }
    l3[l3_idx] = desc;

    if (root == current_ttbr0 || root == kernel_root_ttbr) {
        aarch64_tlbi_va(va);
    }
    return 1;
}

int xscore_vmm_unmap_page_in(uint64_t root, uint64_t va, uint64_t *pa_out) {
    if (va >= 0xFFFF000000000000ULL) {
        root = kernel_root_ttbr;
    }
    if (!root) return 0;
    uint64_t *l0 = (uint64_t*)(uintptr_t)root;
    uint64_t l0_idx = (va >> 39) & 0x1FF;
    uint64_t l1_idx = (va >> 30) & 0x1FF;
    uint64_t l2_idx = (va >> 21) & 0x1FF;
    uint64_t l3_idx = (va >> 12) & 0x1FF;

    if (!(l0[l0_idx] & 1)) return 0;
    uint64_t *l1 = (uint64_t*)(uintptr_t)(l0[l0_idx] & ~0xFFFULL);
    if (!(l1[l1_idx] & 1)) return 0;
    uint64_t *l2 = (uint64_t*)(uintptr_t)(l1[l1_idx] & ~0xFFFULL);
    if (!(l2[l2_idx] & 1)) return 0;
    uint64_t *l3 = (uint64_t*)(uintptr_t)(l2[l2_idx] & ~0xFFFULL);
    if (!(l3[l3_idx] & 1)) return 0;

    if (pa_out) *pa_out = l3[l3_idx] & ~0xFFFULL;
    l3[l3_idx] = 0;

    if (root == current_ttbr0 || root == kernel_root_ttbr) {
        aarch64_tlbi_va(va);
    }
    return 1;
}

uint64_t xscore_vmm_address_space_create(void) {
    uint64_t new_root_phys = xscore_pmm_allocate_page();
    if (!new_root_phys) return 0;
    uint64_t *new_root = (uint64_t*)(uintptr_t)new_root_phys;
    uint64_t *kroot = (uint64_t*)(uintptr_t)kernel_root_ttbr;
    for (int i = 0; i < 512; ++i) {
        new_root[i] = kroot ? kroot[i] : 0;
    }
    return new_root_phys;
}

void xscore_vmm_address_space_destroy(uint64_t root) {
    if (!root || root == kernel_root_ttbr) return;
    xscore_pmm_free_page(root);
}

int xscore_vmm_address_space_activate(uint64_t root) {
    if (!root) return 0;
    current_ttbr0 = root;
    __asm__ volatile("msr ttbr0_el1, %0\n isb" :: "r"(root) : "memory");
    aarch64_tlbi_all();
    return 1;
}

int xscore_vmm_activate(uint64_t root) {
    return xscore_vmm_address_space_activate(root);
}

void xscore_vmm_address_space_sync_kernel(uint64_t root) {
    (void)root;
}

int xscore_vmm_is_mapped(uint64_t va, uint64_t *flags_out) {
    uint64_t root = (va >= 0xFFFF000000000000ULL) ? kernel_root_ttbr : current_ttbr0;
    if (!root) return 0;
    uint64_t *l0 = (uint64_t*)(uintptr_t)root;
    uint64_t l0_idx = (va >> 39) & 0x1FF;
    uint64_t l1_idx = (va >> 30) & 0x1FF;
    uint64_t l2_idx = (va >> 21) & 0x1FF;
    uint64_t l3_idx = (va >> 12) & 0x1FF;

    if (!(l0[l0_idx] & 1)) return 0;
    uint64_t *l1 = (uint64_t*)(uintptr_t)(l0[l0_idx] & ~0xFFFULL);
    if (!(l1[l1_idx] & 1)) return 0;
    uint64_t *l2 = (uint64_t*)(uintptr_t)(l1[l1_idx] & ~0xFFFULL);
    if (!(l2[l2_idx] & 1)) return 0;
    uint64_t *l3 = (uint64_t*)(uintptr_t)(l2[l2_idx] & ~0xFFFULL);
    if (!(l3[l3_idx] & 1)) return 0;

    if (flags_out) {
        uint64_t f = XSCORE_PAGE_PRESENT;
        if (!(l3[l3_idx] & (1ULL << 7))) {
            f |= XSCORE_PAGE_WRITABLE;
        }
        if (l3[l3_idx] & (1ULL << 6)) {
            f |= XSCORE_PAGE_USER;
        }
        if (l3[l3_idx] & (1ULL << 54)) {
            f |= XSCORE_PAGE_NO_EXECUTE;
        }
        *flags_out = f;
    }
    return 1;
}

uint64_t xscore_vmm_current_root(void) {
    return current_ttbr0;
}

uint64_t xscore_vmm_root_table(void) {
    return kernel_root_ttbr;
}

int xscore_vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    uint64_t root = (va >= 0xFFFF000000000000ULL) ? kernel_root_ttbr : current_ttbr0;
    return xscore_vmm_map_page_in(root, va, pa, flags);
}

int xscore_vmm_unmap_page(uint64_t va, uint64_t *pa) {
    uint64_t root = (va >= 0xFFFF000000000000ULL) ? kernel_root_ttbr : current_ttbr0;
    return xscore_vmm_unmap_page_in(root, va, pa);
}

void xscore_vmm_aarch64_init(void) {
    uint64_t root_phys = xscore_pmm_allocate_page();
    kernel_root_ttbr = root_phys;
    current_ttbr0 = 0;

    uint64_t *l0 = (uint64_t*)(uintptr_t)root_phys;
    for (int i = 0; i < 512; ++i) l0[i] = 0;

    uint64_t mair = (0xFFULL << 0) | (0x04ULL << 8);
    __asm__ volatile("msr mair_el1, %0\n isb" :: "r"(mair) : "memory");

    uint64_t tcr = (16ULL << 0) | (1ULL << 8) | (1ULL << 10) | (3ULL << 12) | (0ULL << 14) |
                   (16ULL << 16) | (1ULL << 24) | (1ULL << 26) | (3ULL << 28) | (2ULL << 30) |
                   (2ULL << 32);
    __asm__ volatile("msr tcr_el1, %0\n isb" :: "r"(tcr) : "memory");

    for (uint64_t addr = 0x40000000; addr < 0x48000000; addr += 4096) {
        xscore_vmm_map_page_in(kernel_root_ttbr, addr, addr, XSCORE_PAGE_WRITABLE);
    }

    xscore_vmm_map_page_in(kernel_root_ttbr, 0x09000000, 0x09000000, XSCORE_PAGE_WRITABLE);
    xscore_vmm_map_page_in(kernel_root_ttbr, 0x09010000, 0x09010000,
                          XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE);

    for (uint64_t addr = 0x4010000000ULL; addr < 0x4010200000ULL; addr += 4096) {
        xscore_vmm_map_page_in(kernel_root_ttbr, addr, addr, XSCORE_PAGE_WRITABLE);
    }

    current_ttbr0 = root_phys;
    __asm__ volatile("msr ttbr0_el1, %0\n isb" :: "r"(kernel_root_ttbr) : "memory");
    __asm__ volatile("msr ttbr1_el1, %0\n isb" :: "r"(kernel_root_ttbr) : "memory");

    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0) | (1ULL << 2) | (1ULL << 12);
    __asm__ volatile("msr sctlr_el1, %0\n isb" :: "r"(sctlr) : "memory");
    aarch64_tlbi_all();
}
