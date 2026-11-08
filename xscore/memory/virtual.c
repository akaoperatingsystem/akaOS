#include <xscore/memory.h>
#include <xscore/smp.h>
#include <xscore/vmm.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE UINT64_C(4096)
#define LARGE_PAGE_SIZE (UINT64_C(2) * 1024 * 1024)
#define PAGE_ADDRESS_MASK UINT64_C(0x000ffffffffff000)
#define LARGE_PAGE_FLAG UINT64_C(0x080)
#define ENTRIES_PER_TABLE 512U

static uint64_t root_table_address;
static uint64_t page_table_page_count;
/* All page-table reads and mutations share one lock.  Process roots are
 * private, but their intermediate tables and the kernel-root branches can be
 * created or inspected concurrently by BSP/AP user threads. */
static xscore_spinlock_t vmm_lock = XSCORE_SPINLOCK_INIT;
/* Page-table allocation accounting is global even though each process owns
 * its user tables.  Keep the counter coherent while CPUs create spaces. */
static xscore_spinlock_t vmm_allocation_lock = XSCORE_SPINLOCK_INIT;

static void zero_page(uint64_t physical_address) {
    volatile uint64_t *words = (volatile uint64_t *)(uintptr_t)physical_address;
    for (size_t index = 0; index < PAGE_SIZE / sizeof(uint64_t); ++index) words[index] = 0;
}

static uint64_t allocate_table(void) {
    uint64_t table = xscore_pmm_allocate_page();
    if (!table) return 0;
    zero_page(table);
    uintptr_t flags = xscore_spin_lock_irqsave(&vmm_allocation_lock);
    ++page_table_page_count;
    xscore_spin_unlock_irqrestore(&vmm_allocation_lock, flags);
    return table;
}

/* Acquire the global page-table lock without masking interrupts while
 * waiting.  TLB shootdowns are delivered by IPIs; disabling interrupts before
 * a cross-CPU lock acquisition would let a waiter deadlock the CPU currently
 * broadcasting that IPI. */
static uintptr_t vmm_lock_acquire(void) {
    /* Keep interrupts disabled for the successful compare/exchange so an
     * interrupt cannot re-enter code that is already holding vmm_lock.  If
     * another CPU owns the lock, temporarily enable interrupts while waiting
     * so that CPU can receive and acknowledge a TLB shootdown IPI. */
    uintptr_t flags = xscore_irq_save();
    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&vmm_lock.locked, &expected, 1,
                                        0, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED)) {
            return flags;
        }
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("sti" ::: "memory");
        while (__atomic_load_n(&vmm_lock.locked, __ATOMIC_ACQUIRE)) {
            __asm__ volatile("pause");
        }
        __asm__ volatile("cli" ::: "memory");
#else
        xscore_spin_lock(&vmm_lock);
        return flags;
#endif
    }
}

static void vmm_lock_release(uintptr_t flags) {
    xscore_spin_unlock(&vmm_lock);
    xscore_irq_restore(flags);
}

static inline void write_cr3(uint64_t value) {
    __asm__ volatile("movq %0, %%cr3" : : "r"(value) : "memory");
}

static inline uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile("movq %%cr3, %0" : "=r"(value));
    return value;
}

static void enable_write_protect(void) {
    uint64_t cr0;
    __asm__ volatile("movq %%cr0, %0" : "=r"(cr0));
    cr0 |= UINT64_C(1) << 16;
    __asm__ volatile("movq %0, %%cr0" : : "r"(cr0) : "memory");
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    if (value > UINT64_MAX - (alignment - 1)) return UINT64_MAX & ~(alignment - 1);
    return (value + alignment - 1) & ~(alignment - 1);
}

#define MSR_EFER UINT64_C(0xC0000080)
#define EFER_NXE (UINT64_C(1) << 11)

static inline uint64_t rdmsr_k(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr_k(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

/*
 * Enable the NX (no-execute) paging feature in EFER. This must happen
 * before any page-table entry uses bit 63: while EFER.NXE is clear that bit
 * is reserved and would fault when the page is accessed.
 */
static void enable_nxe(void) {
    uint64_t efer = rdmsr_k(MSR_EFER);
    if (!(efer & EFER_NXE)) wrmsr_k(MSR_EFER, efer | EFER_NXE);
}

int xscore_vmm_initialize(const struct akaos_boot_info *boot_info,
                          const struct xscore_physical_memory_stats *physical_stats,
                          struct xscore_virtual_memory_stats *virtual_stats) {
    if (!boot_info || !physical_stats || !virtual_stats) return 0;
    enable_nxe();
    uint64_t framebuffer_end = boot_info->framebuffer.address + boot_info->framebuffer.size;
    if (framebuffer_end < boot_info->framebuffer.address) return 0;
    uint64_t identity_limit = physical_stats->highest_address;
    if (framebuffer_end > identity_limit) identity_limit = framebuffer_end;
    identity_limit = align_up(identity_limit, LARGE_PAGE_SIZE);
    if (identity_limit == 0 || identity_limit > UINT64_C(512) * 1024 * 1024 * 1024) return 0;

    page_table_page_count = 0;
    root_table_address = allocate_table();
    uint64_t pdpt_address = allocate_table();
    if (!root_table_address || !pdpt_address) return 0;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)root_table_address;
    uint64_t *pdpt = (uint64_t *)(uintptr_t)pdpt_address;
    pml4[0] = pdpt_address | XSCORE_PAGE_PRESENT | XSCORE_PAGE_WRITABLE;

    uint64_t mapped = 0;
    for (uint64_t pdpt_index = 0; mapped < identity_limit; ++pdpt_index) {
        if (pdpt_index >= ENTRIES_PER_TABLE) return 0;
        uint64_t directory_address = allocate_table();
        if (!directory_address) return 0;
        pdpt[pdpt_index] = directory_address | XSCORE_PAGE_PRESENT | XSCORE_PAGE_WRITABLE;
        uint64_t *directory = (uint64_t *)(uintptr_t)directory_address;
        for (uint64_t entry = 0; entry < ENTRIES_PER_TABLE && mapped < identity_limit; ++entry) {
            if (mapped == 0) {
                uint64_t table_address = allocate_table();
                if (!table_address) return 0;
                directory[entry] = table_address | XSCORE_PAGE_PRESENT | XSCORE_PAGE_WRITABLE;
                uint64_t *table = (uint64_t *)(uintptr_t)table_address;
                for (uint64_t page = 1; page < ENTRIES_PER_TABLE; ++page) {
                    table[page] = page * PAGE_SIZE | XSCORE_PAGE_PRESENT | XSCORE_PAGE_WRITABLE;
                }
                mapped = LARGE_PAGE_SIZE;
            } else {
                directory[entry] = mapped | XSCORE_PAGE_PRESENT | XSCORE_PAGE_WRITABLE | LARGE_PAGE_FLAG;
                mapped += LARGE_PAGE_SIZE;
            }
        }
    }

    write_cr3(root_table_address);
    enable_write_protect();
    if ((read_cr3() & PAGE_ADDRESS_MASK) != root_table_address) return 0;

    virtual_stats->identity_mapped_bytes = mapped;
    virtual_stats->page_table_pages = page_table_page_count;
    virtual_stats->root_table_address = root_table_address;
    virtual_stats->null_page_guarded = 1;
    return 1;
}

static uint64_t *next_level(uint64_t *table, uint16_t index, int create, uint64_t branch_flags) {
    uint64_t entry = table[index];
    if ((entry & XSCORE_PAGE_PRESENT) == 0) {
        if (!create) return 0;
        uint64_t child = allocate_table();
        if (!child) return 0;
        table[index] = child | XSCORE_PAGE_PRESENT | XSCORE_PAGE_WRITABLE | (branch_flags & XSCORE_PAGE_USER);
        entry = table[index];
    }
    if (entry & LARGE_PAGE_FLAG) return 0;
    if (branch_flags & XSCORE_PAGE_USER) table[index] |= XSCORE_PAGE_USER;
    return (uint64_t *)(uintptr_t)(entry & PAGE_ADDRESS_MASK);
}

static int address_is_canonical(uint64_t address) {
    uint64_t upper = address >> 48;
    return ((address >> 47) & 1) ? upper == UINT64_C(0xffff) : upper == 0;
}

int xscore_vmm_map_page_in(uint64_t root_address, uint64_t virtual_address,
                           uint64_t physical_address, uint64_t flags) {
    if (!root_address || virtual_address == 0 || !address_is_canonical(virtual_address) ||
        (virtual_address & (PAGE_SIZE - 1)) != 0 ||
        (physical_address & (PAGE_SIZE - 1)) != 0) return 0;
    uintptr_t irq_flags = vmm_lock_acquire();
    int result = 0;
    uint16_t pml4_index = (uint16_t)((virtual_address >> 39) & 0x1ff);
    uint16_t pdpt_index = (uint16_t)((virtual_address >> 30) & 0x1ff);
    uint16_t directory_index = (uint16_t)((virtual_address >> 21) & 0x1ff);
    uint16_t table_index = (uint16_t)((virtual_address >> 12) & 0x1ff);
    uint64_t *pml4 = (uint64_t *)(uintptr_t)root_address;
    uint64_t *pdpt = next_level(pml4, pml4_index, 1, flags);
    if (!pdpt) goto out;
    uint64_t *directory = next_level(pdpt, pdpt_index, 1, flags);
    if (!directory) goto out;
    uint64_t *table = next_level(directory, directory_index, 1, flags);
    if (!table || (table[table_index] & XSCORE_PAGE_PRESENT)) goto out;
    table[table_index] = physical_address | flags | XSCORE_PAGE_PRESENT;
    result = 1;
out:
    vmm_lock_release(irq_flags);
    /* Do not wait for remote TLB acknowledgements while holding vmm_lock.
     * A CPU can enter a page-fault handler with interrupts masked and need
     * this same lock; waiting here would deadlock the sender against that
     * handler's pending IPI. */
    if (result) xscore_smp_tlb_shootdown(root_address, virtual_address);
    return result;
}

int xscore_vmm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags) {
    return xscore_vmm_map_page_in(read_cr3() & PAGE_ADDRESS_MASK, virtual_address,
                                  physical_address, flags);
}

int xscore_vmm_unmap_page_in(uint64_t root_address, uint64_t virtual_address,
                             uint64_t *physical_address) {
    if (!root_address || !address_is_canonical(virtual_address) ||
        (virtual_address & (PAGE_SIZE - 1)) != 0) return 0;
    uintptr_t irq_flags = vmm_lock_acquire();
    int result = 0;
    uint16_t pml4_index = (uint16_t)((virtual_address >> 39) & 0x1ff);
    uint16_t pdpt_index = (uint16_t)((virtual_address >> 30) & 0x1ff);
    uint16_t directory_index = (uint16_t)((virtual_address >> 21) & 0x1ff);
    uint16_t table_index = (uint16_t)((virtual_address >> 12) & 0x1ff);
    uint64_t *pml4 = (uint64_t *)(uintptr_t)root_address;
    uint64_t *pdpt = next_level(pml4, pml4_index, 0, 0);
    if (!pdpt) goto out;
    uint64_t *directory = next_level(pdpt, pdpt_index, 0, 0);
    if (!directory) goto out;
    uint64_t *table = next_level(directory, directory_index, 0, 0);
    if (!table || (table[table_index] & XSCORE_PAGE_PRESENT) == 0) goto out;
    if (physical_address) *physical_address = table[table_index] & PAGE_ADDRESS_MASK;
    table[table_index] = 0;
    result = 1;
out:
    vmm_lock_release(irq_flags);
    if (result) xscore_smp_tlb_shootdown(root_address, virtual_address);
    return result;
}

int xscore_vmm_unmap_page(uint64_t virtual_address, uint64_t *physical_address) {
    return xscore_vmm_unmap_page_in(read_cr3() & PAGE_ADDRESS_MASK, virtual_address,
                                    physical_address);
}

/*
 * Split a 2 MiB large page at the given (2 MiB aligned) address into 512
 * individual 4 KiB pages, preserving the existing mapping and its
 * present/writable/user/execute flags. Returns 1 on success; also returns 1
 * if the entry is already 4 KiB page granularity or unmapped (nothing to do).
 *
 * This is required before per-section permission changes can be applied:
 * large pages have a single set of flags for the whole 2 MiB.
 */
int xscore_vmm_split_large_page(uint64_t virtual_address) {
    if (!root_table_address || !address_is_canonical(virtual_address) ||
        (virtual_address & (LARGE_PAGE_SIZE - 1)) != 0) return 0;
    uintptr_t irq_flags = vmm_lock_acquire();
    int result = 0;
    uint16_t pml4_index = (uint16_t)((virtual_address >> 39) & 0x1ff);
    uint16_t pdpt_index = (uint16_t)((virtual_address >> 30) & 0x1ff);
    uint16_t directory_index = (uint16_t)((virtual_address >> 21) & 0x1ff);
    uint64_t *pml4 = (uint64_t *)(uintptr_t)root_table_address;
    uint64_t *pdpt = next_level(pml4, pml4_index, 0, 0);
    if (!pdpt) goto out;
    uint64_t *directory = next_level(pdpt, pdpt_index, 0, 0);
    if (!directory) goto out;
    uint64_t entry = directory[directory_index];
    if ((entry & XSCORE_PAGE_PRESENT) == 0 || (entry & LARGE_PAGE_FLAG) == 0) {
        result = 1;
        goto out;
    }

    uint64_t table_address = allocate_table();
    if (!table_address) goto out;
    uint64_t *table = (uint64_t *)(uintptr_t)table_address;
    uint64_t physical_base = entry & PAGE_ADDRESS_MASK;
    /* Preserve present/writable/user flags; the large page may be
     * executable (NX cleared) as before — permission changes come later. */
    uint64_t page_flags = entry & (XSCORE_PAGE_PRESENT | XSCORE_PAGE_WRITABLE |
                                   XSCORE_PAGE_USER | XSCORE_PAGE_NO_EXECUTE);
    for (uint64_t index = 0; index < ENTRIES_PER_TABLE; ++index) {
        table[index] = (physical_base + index * PAGE_SIZE) | page_flags;
    }
    uint64_t branch_flags = XSCORE_PAGE_PRESENT | XSCORE_PAGE_WRITABLE;
    if (entry & XSCORE_PAGE_USER) branch_flags |= XSCORE_PAGE_USER;
    directory[directory_index] = table_address | branch_flags;
    /* INVLPG applied to an address covered by a large-page entry invalidates
     * that entry on x86; broadcast the aligned base to every online CPU. */
    result = 1;
out:
    vmm_lock_release(irq_flags);
    if (result) xscore_smp_tlb_shootdown(root_table_address, virtual_address);
    return result;
}

/*
 * Report whether the 4 KiB page at virtual_address is mapped, and if so
 * store its current flags (without the physical address bits) in *flags.
 */
int xscore_vmm_is_mapped_in(uint64_t root_address, uint64_t virtual_address,
                            uint64_t *flags, uint64_t *physical_address) {
    if (!root_address || !address_is_canonical(virtual_address) ||
        (virtual_address & (PAGE_SIZE - 1)) != 0) return 0;
    uintptr_t irq_flags = vmm_lock_acquire();
    int result = 0;
    uint16_t pml4_index = (uint16_t)((virtual_address >> 39) & 0x1ff);
    uint16_t pdpt_index = (uint16_t)((virtual_address >> 30) & 0x1ff);
    uint16_t directory_index = (uint16_t)((virtual_address >> 21) & 0x1ff);
    uint16_t table_index = (uint16_t)((virtual_address >> 12) & 0x1ff);
    uint64_t *pml4 = (uint64_t *)(uintptr_t)root_address;
    uint64_t *pdpt = next_level(pml4, pml4_index, 0, 0);
    if (!pdpt) goto out;
    uint64_t *directory = next_level(pdpt, pdpt_index, 0, 0);
    if (!directory) goto out;
    uint64_t *table = next_level(directory, directory_index, 0, 0);
    if (!table) goto out;
    uint64_t entry = table[table_index];
    if ((entry & XSCORE_PAGE_PRESENT) == 0) goto out;
    if (flags) *flags = entry & ~PAGE_ADDRESS_MASK;
    if (physical_address) *physical_address = entry & PAGE_ADDRESS_MASK;
    result = 1;
out:
    vmm_lock_release(irq_flags);
    return result;
}

int xscore_vmm_is_mapped(uint64_t virtual_address, uint64_t *flags) {
    return xscore_vmm_is_mapped_in(read_cr3() & PAGE_ADDRESS_MASK, virtual_address,
                                   flags, 0);
}

int xscore_vmm_update_page_flags(uint64_t virtual_address, uint64_t flags) {
    if (!root_table_address || !address_is_canonical(virtual_address) ||
        (virtual_address & (PAGE_SIZE - 1)) != 0) return 0;
    uintptr_t irq_flags = vmm_lock_acquire();
    int result = 0;
    uint16_t pml4_index = (uint16_t)((virtual_address >> 39) & 0x1ff);
    uint16_t pdpt_index = (uint16_t)((virtual_address >> 30) & 0x1ff);
    uint16_t directory_index = (uint16_t)((virtual_address >> 21) & 0x1ff);
    uint16_t table_index = (uint16_t)((virtual_address >> 12) & 0x1ff);
    uint64_t *pml4 = (uint64_t *)(uintptr_t)root_table_address;
    uint64_t *pdpt = next_level(pml4, pml4_index, 0, 0);
    if (!pdpt) goto out;
    uint64_t *directory = next_level(pdpt, pdpt_index, 0, 0);
    if (!directory) goto out;
    uint64_t *table = next_level(directory, directory_index, 0, 0);
    if (!table || (table[table_index] & XSCORE_PAGE_PRESENT) == 0) goto out;
    uint64_t physical = table[table_index] & PAGE_ADDRESS_MASK;
    table[table_index] = physical | flags | XSCORE_PAGE_PRESENT;
    result = 1;
out:
    vmm_lock_release(irq_flags);
    if (result) xscore_smp_tlb_shootdown(root_table_address, virtual_address);
    return result;
}

uint64_t xscore_vmm_root_table(void) {
    return root_table_address;
}

uint64_t xscore_vmm_current_root(void) {
    return read_cr3() & PAGE_ADDRESS_MASK;
}

int xscore_vmm_activate(uint64_t root_address) {
    if (!root_address || (root_address & (PAGE_SIZE - 1)) != 0) return 0;
    if ((read_cr3() & PAGE_ADDRESS_MASK) != root_address) {
        write_cr3(root_address);
    }
    return 1;
}

uint64_t xscore_vmm_address_space_create(void) {
    if (!root_table_address) return 0;
    uintptr_t irq_flags = vmm_lock_acquire();
    uint64_t new_root = allocate_table();
    if (!new_root) {
        vmm_lock_release(irq_flags);
        return 0;
    }
    uint64_t *kernel_pml4 = (uint64_t *)(uintptr_t)root_table_address;
    uint64_t *process_pml4 = (uint64_t *)(uintptr_t)new_root;
    for (uint32_t index = 0; index < ENTRIES_PER_TABLE; ++index) {
        uint64_t entry = kernel_pml4[index];
        if ((entry & XSCORE_PAGE_PRESENT) && (entry & XSCORE_PAGE_USER) == 0) {
            process_pml4[index] = entry;
        }
    }
    vmm_lock_release(irq_flags);
    return new_root;
}

void xscore_vmm_address_space_sync_kernel(uint64_t root_address) {
    if (!root_table_address || !root_address) return;
    uintptr_t irq_flags = vmm_lock_acquire();
    uint64_t *kernel_pml4 = (uint64_t *)(uintptr_t)root_table_address;
    uint64_t *process_pml4 = (uint64_t *)(uintptr_t)root_address;
    for (uint32_t index = 0; index < ENTRIES_PER_TABLE; ++index) {
        uint64_t entry = kernel_pml4[index];
        if ((entry & XSCORE_PAGE_PRESENT) && (entry & XSCORE_PAGE_USER) == 0) {
            process_pml4[index] = entry;
        }
    }
    vmm_lock_release(irq_flags);
}

static void free_user_tables(uint64_t table_address, uint32_t level) {
    uint64_t *table = (uint64_t *)(uintptr_t)table_address;
    for (uint32_t index = 0; index < ENTRIES_PER_TABLE; ++index) {
        uint64_t entry = table[index];
        if ((entry & (XSCORE_PAGE_PRESENT | XSCORE_PAGE_USER)) !=
            (XSCORE_PAGE_PRESENT | XSCORE_PAGE_USER)) continue;
        if (level > 1 && (entry & LARGE_PAGE_FLAG) == 0) {
            uint64_t child = entry & PAGE_ADDRESS_MASK;
            free_user_tables(child, level - 1);
            xscore_pmm_free_page(child);
        }
        table[index] = 0;
    }
}

void xscore_vmm_address_space_destroy(uint64_t root_address) {
    if (!root_address || root_address == root_table_address ||
        root_address == (read_cr3() & PAGE_ADDRESS_MASK)) return;
    uintptr_t irq_flags = vmm_lock_acquire();
    free_user_tables(root_address, 4);
    xscore_pmm_free_page(root_address);
    vmm_lock_release(irq_flags);
}
