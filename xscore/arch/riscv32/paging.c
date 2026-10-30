#include <xscore/vmm.h>
#include <xscore/memory.h>
#include <stdint.h>
#include <stddef.h>

#define PTE_V (1U << 0)
#define PTE_R (1U << 1)
#define PTE_W (1U << 2)
#define PTE_X (1U << 3)
#define PTE_U (1U << 4)
#define PTE_G (1U << 5)
#define PTE_A (1U << 6)
#define PTE_D (1U << 7)

static uint64_t kernel_root_satp = 0;
static uint64_t current_satp = 0;

static inline void sfence_vma(void) {
    __asm__ volatile("sfence.vma" ::: "memory");
}

int xscore_vmm_map_page_in(uint64_t root, uint64_t va, uint64_t pa, uint64_t flags) {
    if (!root) root = kernel_root_satp;
    if (!root) return 0;

    uint32_t *l1 = (uint32_t*)(uintptr_t)root;
    uint32_t vpn1 = ((uint32_t)va >> 22) & 0x3FF;
    uint32_t vpn0 = ((uint32_t)va >> 12) & 0x3FF;

    if (!(l1[vpn1] & PTE_V)) {
        uint64_t l0_phys = xscore_pmm_allocate_page();
        if (!l0_phys) return 0;
        uint32_t *l0_tbl = (uint32_t*)(uintptr_t)l0_phys;
        for (int i = 0; i < 1024; ++i) l0_tbl[i] = 0;
        l1[vpn1] = (((uint32_t)l0_phys >> 12) << 10) | PTE_V;
    }
    if (root == kernel_root_satp && (vpn1 < 256 || vpn1 >= 512) && current_satp && current_satp != kernel_root_satp) {
        uint32_t *cur_l1 = (uint32_t*)(uintptr_t)current_satp;
        cur_l1[vpn1] = l1[vpn1];
    }

    uint32_t *l0 = (uint32_t*)(uintptr_t)(((l1[vpn1] >> 10) << 12));

    uint32_t pte = (((uint32_t)pa >> 12) << 10) | PTE_V | PTE_R | PTE_A | PTE_D;
    if (flags & XSCORE_PAGE_WRITABLE) pte |= PTE_W;
    if (!(flags & XSCORE_PAGE_NO_EXECUTE)) pte |= PTE_X;
    if (flags & XSCORE_PAGE_USER) pte |= PTE_U;

    l0[vpn0] = pte;
    sfence_vma();
    return 1;
}

int xscore_vmm_unmap_page_in(uint64_t root, uint64_t va, uint64_t *pa_out) {
    if (!root) root = kernel_root_satp;
    if (!root) return 0;

    uint32_t *l1 = (uint32_t*)(uintptr_t)root;
    uint32_t vpn1 = ((uint32_t)va >> 22) & 0x3FF;
    uint32_t vpn0 = ((uint32_t)va >> 12) & 0x3FF;

    if (!(l1[vpn1] & PTE_V)) return 0;
    uint32_t *l0 = (uint32_t*)(uintptr_t)(((l1[vpn1] >> 10) << 12));
    if (!(l0[vpn0] & PTE_V)) return 0;

    if (pa_out) {
        *pa_out = (uint64_t)((l0[vpn0] >> 10) << 12);
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
    uint32_t *new_root = (uint32_t*)(uintptr_t)new_root_phys;
    uint32_t *kroot = (uint32_t*)(uintptr_t)kernel_root_satp;
    for (int i = 0; i < 1024; ++i) {
        new_root[i] = kroot ? kroot[i] : 0;
    }
    return new_root_phys;
}

void xscore_vmm_address_space_destroy(uint64_t root) {
    if (!root || root == kernel_root_satp) return;
    uint32_t *l1 = (uint32_t*)(uintptr_t)root;
    uint32_t *kroot = (uint32_t*)(uintptr_t)kernel_root_satp;

    for (int i = 256; i < 512; ++i) {
        if (kroot && l1[i] == kroot[i]) continue;
        if (l1[i] & PTE_V) {
            xscore_pmm_free_page((uint64_t)((l1[i] >> 10) << 12));
        }
    }
    xscore_pmm_free_page(root);
}

int xscore_vmm_address_space_activate(uint64_t root) {
    if (!root) return 0;
    current_satp = root;
    xscore_vmm_address_space_sync_kernel(root);
    uint32_t satp_val = (1U << 31) | ((uint32_t)root >> 12);
    __asm__ volatile("csrw satp, %0\n sfence.vma" :: "r"(satp_val) : "memory");
    return 1;
}

int xscore_vmm_activate(uint64_t root) {
    return xscore_vmm_address_space_activate(root ? root : kernel_root_satp);
}

void xscore_vmm_address_space_sync_kernel(uint64_t root) {
    if (!root || root == kernel_root_satp) return;
    uint32_t *dst = (uint32_t*)(uintptr_t)root;
    uint32_t *src = (uint32_t*)(uintptr_t)kernel_root_satp;
    if (!src) return;
    for (int i = 0; i < 256; ++i) {
        dst[i] = src[i];
    }
    for (int i = 512; i < 1024; ++i) {
        dst[i] = src[i];
    }
}

int xscore_vmm_is_mapped(uint64_t va, uint64_t *flags_out) {
    uint64_t root = current_satp ? current_satp : kernel_root_satp;
    if (!root) return 0;
    uint32_t *l1 = (uint32_t*)(uintptr_t)root;

    uint32_t vpn1 = ((uint32_t)va >> 22) & 0x3FF;
    uint32_t vpn0 = ((uint32_t)va >> 12) & 0x3FF;

    if (!(l1[vpn1] & PTE_V)) return 0;
    uint32_t *l0 = (uint32_t*)(uintptr_t)(((l1[vpn1] >> 10) << 12));
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

void xscore_vmm_riscv32_init(void) {
    kernel_root_satp = xscore_pmm_allocate_page();
    if (!kernel_root_satp) return;
    uint32_t *root_l1 = (uint32_t*)(uintptr_t)kernel_root_satp;
    for (int i = 0; i < 1024; ++i) root_l1[i] = 0;

    /* Identity map RAM: 0x80000000 to 0x88000000 (128 MB) */
    for (uint32_t addr = 0x80000000U; addr < 0x88000000U; addr += 0x1000) {
        xscore_vmm_map_page_in(kernel_root_satp, addr, addr,
                              XSCORE_PAGE_WRITABLE);
    }

    /* Identity map NS16550A UART MMIO: 0x10000000 to 0x10010000 (64 KB) */
    for (uint32_t addr = 0x10000000U; addr < 0x10010000U; addr += 0x1000) {
        xscore_vmm_map_page_in(kernel_root_satp, addr, addr,
                              XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE);
    }


    /* QEMU Goldfish real-time clock. */
    xscore_vmm_map_page_in(kernel_root_satp, UINT32_C(0x00101000),
                          UINT32_C(0x00101000),
                          XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE);

    /* Identity map PCI ECAM: 0x30000000 to 0x30200000 (2 MB) */
    for (uint32_t addr = 0x30000000U; addr < 0x30200000U; addr += 0x1000) {
        xscore_vmm_map_page_in(kernel_root_satp, addr, addr,
                              XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE);
    }

    /* Activate Sv32 */
    xscore_vmm_address_space_activate(kernel_root_satp);
}
