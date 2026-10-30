#include <xscore/vmm.h>
#include <xscore/memory.h>
#include <stdint.h>
#include <stddef.h>

#define PTE_V (1ULL << 0)
#define PTE_R (1ULL << 1)
#define PTE_W (1ULL << 2)
#define PTE_X (1ULL << 3)
#define PTE_U (1ULL << 4)
#define PTE_G (1ULL << 5)
#define PTE_A (1ULL << 6)
#define PTE_D (1ULL << 7)

static uint64_t kernel_root_satp = 0;
static uint64_t current_satp = 0;

static inline void sfence_vma(void) {
    __asm__ volatile("sfence.vma" ::: "memory");
}

int xscore_vmm_map_page_in(uint64_t root, uint64_t va, uint64_t pa, uint64_t flags) {
    if (va >= 0xFFFFFFC000000000ULL || ((va >> 38) & 1)) {
        root = kernel_root_satp;
    }
    if (!root) return 0;
    uint64_t *l2 = (uint64_t*)(uintptr_t)root;

    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    if (!(l2[vpn2] & PTE_V)) {
        uint64_t l1_phys = xscore_pmm_allocate_page();
        if (!l1_phys) return 0;
        uint64_t *l1_tbl = (uint64_t*)(uintptr_t)l1_phys;
        for (int i = 0; i < 512; ++i) l1_tbl[i] = 0;
        l2[vpn2] = ((l1_phys >> 12) << 10) | PTE_V;
    }
    if (root == kernel_root_satp && vpn2 >= 256 && current_satp && current_satp != kernel_root_satp) {
        uint64_t *cur_l2 = (uint64_t*)(uintptr_t)current_satp;
        cur_l2[vpn2] = l2[vpn2];
    }
    uint64_t *l1 = (uint64_t*)(uintptr_t)((l2[vpn2] >> 10) << 12);

    if (!(l1[vpn1] & PTE_V)) {
        uint64_t l0_phys = xscore_pmm_allocate_page();
        if (!l0_phys) return 0;
        uint64_t *l0_tbl = (uint64_t*)(uintptr_t)l0_phys;
        for (int i = 0; i < 512; ++i) l0_tbl[i] = 0;
        l1[vpn1] = ((l0_phys >> 12) << 10) | PTE_V;
    }
    uint64_t *l0 = (uint64_t*)(uintptr_t)((l1[vpn1] >> 10) << 12);

    uint64_t pte = ((pa >> 12) << 10) | PTE_V | PTE_R | PTE_A | PTE_D;
    if (flags & XSCORE_PAGE_WRITABLE) pte |= PTE_W;
    if (!(flags & XSCORE_PAGE_NO_EXECUTE)) pte |= PTE_X;
    if (flags & XSCORE_PAGE_USER) pte |= PTE_U;

    l0[vpn0] = pte;
    sfence_vma();
    return 1;
}

int xscore_vmm_unmap_page_in(uint64_t root, uint64_t va, uint64_t *pa_out) {
    if (va >= 0xFFFFFFC000000000ULL || ((va >> 38) & 1)) {
        root = kernel_root_satp;
    }
    if (!root) return 0;
    uint64_t *l2 = (uint64_t*)(uintptr_t)root;

    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    if (!(l2[vpn2] & PTE_V)) return 0;
    uint64_t *l1 = (uint64_t*)(uintptr_t)((l2[vpn2] >> 10) << 12);
    if (!(l1[vpn1] & PTE_V)) return 0;
    uint64_t *l0 = (uint64_t*)(uintptr_t)((l1[vpn1] >> 10) << 12);
    if (!(l0[vpn0] & PTE_V)) return 0;

    if (pa_out) {
        *pa_out = (l0[vpn0] >> 10) << 12;
    }
    l0[vpn0] = 0;
    sfence_vma();
    return 1;
}

uint64_t xscore_vmm_root_table(void) {
    return kernel_root_satp;
}

int xscore_vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    return xscore_vmm_map_page_in(kernel_root_satp, va, pa, flags);
}

int xscore_vmm_unmap_page(uint64_t va, uint64_t *pa_out) {
    return xscore_vmm_unmap_page_in(kernel_root_satp, va, pa_out);
}

int xscore_vmm_update_flags(uint64_t va, uint64_t flags) {
    uint64_t pa = 0;
    if (!xscore_vmm_unmap_page_in(kernel_root_satp, va, &pa)) return 0;
    return xscore_vmm_map_page_in(kernel_root_satp, va, pa, flags);
}

uint64_t xscore_vmm_address_space_create(void) {
    uint64_t new_root_phys = xscore_pmm_allocate_page();
    if (!new_root_phys) return 0;
    uint64_t *new_root = (uint64_t*)(uintptr_t)new_root_phys;
    uint64_t *kroot = (uint64_t*)(uintptr_t)kernel_root_satp;
    for (int i = 0; i < 512; ++i) {
        new_root[i] = kroot ? kroot[i] : 0;
    }
    return new_root_phys;
}

void xscore_vmm_address_space_destroy(uint64_t root) {
    if (!root || root == kernel_root_satp) return;
    uint64_t *l2 = (uint64_t*)(uintptr_t)root;
    uint64_t *kroot = (uint64_t*)(uintptr_t)kernel_root_satp;

    for (int i = 0; i < 256; ++i) {
        if (kroot && l2[i] == kroot[i]) continue;
        if (l2[i] & PTE_V) {
            uint64_t *l1 = (uint64_t*)(uintptr_t)((l2[i] >> 10) << 12);
            for (int j = 0; j < 512; ++j) {
                if (l1[j] & PTE_V) {
                    xscore_pmm_free_page((l1[j] >> 10) << 12);
                }
            }
            xscore_pmm_free_page((l2[i] >> 10) << 12);
        }
    }
    xscore_pmm_free_page(root);
}

int xscore_vmm_address_space_activate(uint64_t root) {
    if (!root) return 0;
    current_satp = root;
    xscore_vmm_address_space_sync_kernel(root);
    uint64_t satp_val = (8ULL << 60) | (root >> 12);
    __asm__ volatile("csrw satp, %0\n sfence.vma" :: "r"(satp_val) : "memory");
    return 1;
}

int xscore_vmm_activate(uint64_t root) {
    return xscore_vmm_address_space_activate(root ? root : kernel_root_satp);
}

void xscore_vmm_address_space_sync_kernel(uint64_t root) {
    if (!root || root == kernel_root_satp) return;
    uint64_t *dst = (uint64_t*)(uintptr_t)root;
    uint64_t *src = (uint64_t*)(uintptr_t)kernel_root_satp;
    if (!src) return;
    for (int i = 256; i < 512; ++i) {
        dst[i] = src[i];
    }
}

int xscore_vmm_is_mapped(uint64_t va, uint64_t *flags_out) {
    uint64_t root = current_satp ? current_satp : kernel_root_satp;
    if (va >= 0xFFFFFFC000000000ULL || ((va >> 38) & 1)) {
        root = kernel_root_satp;
    }
    if (!root) return 0;
    uint64_t *l2 = (uint64_t*)(uintptr_t)root;

    uint64_t vpn2 = (va >> 30) & 0x1FF;
    uint64_t vpn1 = (va >> 21) & 0x1FF;
    uint64_t vpn0 = (va >> 12) & 0x1FF;

    if (!(l2[vpn2] & PTE_V)) return 0;
    uint64_t *l1 = (uint64_t*)(uintptr_t)((l2[vpn2] >> 10) << 12);
    if (!(l1[vpn1] & PTE_V)) return 0;
    uint64_t *l0 = (uint64_t*)(uintptr_t)((l1[vpn1] >> 10) << 12);
    if (!(l0[vpn0] & PTE_V)) return 0;

    if (flags_out) {
        uint64_t f = XSCORE_PAGE_PRESENT;
        if (l0[vpn0] & PTE_W) f |= XSCORE_PAGE_WRITABLE;
        if (l0[vpn0] & PTE_U) f |= XSCORE_PAGE_USER;
        if (!(l0[vpn0] & PTE_X)) f |= XSCORE_PAGE_NO_EXECUTE;
        *flags_out = f;
    }
    return 1;
}

uint64_t xscore_vmm_current_root(void) {
    return current_satp ? current_satp : kernel_root_satp;
}

void xscore_vmm_riscv64_init(void) {
    kernel_root_satp = xscore_pmm_allocate_page();
    if (!kernel_root_satp) return;
    uint64_t *root_l2 = (uint64_t*)(uintptr_t)kernel_root_satp;
    for (int i = 0; i < 512; ++i) root_l2[i] = 0;

    /* Identity map RAM: 0x80000000 to 0x88000000 (128 MB) */
    for (uint64_t addr = 0x80000000ULL; addr < 0x88000000ULL; addr += 0x1000) {
        xscore_vmm_map_page_in(kernel_root_satp, addr, addr,
                              XSCORE_PAGE_WRITABLE);
    }

    /* Identity map NS16550A UART MMIO: 0x10000000 to 0x10010000 (64 KB) */
    for (uint64_t addr = 0x10000000ULL; addr < 0x10010000ULL; addr += 0x1000) {
        xscore_vmm_map_page_in(kernel_root_satp, addr, addr,
                              XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE);
    }

    /* QEMU Goldfish real-time clock. */
    xscore_vmm_map_page_in(kernel_root_satp, UINT64_C(0x00101000),
                          UINT64_C(0x00101000),
                          XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE);

    /* Identity map PCI ECAM: 0x30000000 to 0x30200000 (2 MB) */
    for (uint64_t addr = 0x30000000ULL; addr < 0x30200000ULL; addr += 0x1000) {
        xscore_vmm_map_page_in(kernel_root_satp, addr, addr,
                              XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE);
    }

    /* Activate Sv39 */
    xscore_vmm_address_space_activate(kernel_root_satp);
}
