#include <xscore/memory.h>
#include <xscore/smp.h>
#include <xscore/serial.h>
#include <xscore/stack.h>
#include <xscore/vmm.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE UINT64_C(4096)
#define LOW_MEMORY_RESERVATION UINT64_C(0x100000)
#define MAX_PHYSICAL_MEMORY (UINT64_C(4) * 1024 * 1024 * 1024)
#define MAX_PAGE_COUNT (MAX_PHYSICAL_MEMORY / PAGE_SIZE)
#define BITMAP_WORD_COUNT (MAX_PAGE_COUNT / 64)
#define EFI_CONVENTIONAL_MEMORY 7U

struct efi_memory_descriptor {
    uint32_t type;
    uint32_t padding;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attributes;
};

static uint64_t allocation_bitmap[BITMAP_WORD_COUNT] __attribute__((aligned(4096)));
static uint64_t managed_page_count;
static uint64_t free_page_count;
static uint64_t next_search_page;
/* SMP safety: bitmap, counters, search hint, and refcount table mutate
 * together. IRQs-off spinlock keeps timer/fault contexts reentrant. */
static xscore_spinlock_t pmm_lock = XSCORE_SPINLOCK_INIT;
static int pmm_locked;

extern char __text_start[];
extern char __kernel_end[];

static void refcount_initialize(void);
struct page_ref_entry;
static struct page_ref_entry *refcount_entry(uint64_t page, int create);

static void mark_page_used(uint64_t page) {
    allocation_bitmap[page / 64] |= UINT64_C(1) << (page % 64);
}

static void mark_page_free(uint64_t page) {
    allocation_bitmap[page / 64] &= ~(UINT64_C(1) << (page % 64));
}

static int page_is_used(uint64_t page) {
    return (allocation_bitmap[page / 64] & (UINT64_C(1) << (page % 64))) != 0;
}

static void mark_range_free(uint64_t physical_start, uint64_t page_count) {
    uint64_t first_page = physical_start / PAGE_SIZE;
    if (first_page >= managed_page_count) return;
    if (page_count > managed_page_count - first_page) page_count = managed_page_count - first_page;
    for (uint64_t page = first_page; page < first_page + page_count; ++page) {
        if (page_is_used(page)) {
            mark_page_free(page);
            ++free_page_count;
        }
    }
}

static void reserve_page(uint64_t page) {
    if (page >= managed_page_count || page_is_used(page)) return;
    mark_page_used(page);
    --free_page_count;
}

static void reserve_range(uint64_t start, uint64_t end) {
    if (end <= start) return;
    uint64_t first = start / PAGE_SIZE;
    uint64_t last = (end + PAGE_SIZE - 1) / PAGE_SIZE;
    if (last < first) return;
    for (uint64_t page = first; page < last; ++page) reserve_page(page);
}

int xscore_pmm_init_direct(uint64_t memory_bytes, uint64_t reserved_end) {
    pmm_locked = 1;
    for (size_t index = 0; index < BITMAP_WORD_COUNT; ++index) {
        allocation_bitmap[index] = UINT64_MAX;
    }
    managed_page_count = memory_bytes / PAGE_SIZE;
    if (managed_page_count > MAX_PAGE_COUNT) managed_page_count = MAX_PAGE_COUNT;
    if (managed_page_count == 0) return 0;
    refcount_initialize();
    free_page_count = 0;
    next_search_page = LOW_MEMORY_RESERVATION / PAGE_SIZE;

    uint64_t low_pages = LOW_MEMORY_RESERVATION / PAGE_SIZE;
    if (managed_page_count > low_pages) {
        mark_range_free(LOW_MEMORY_RESERVATION, managed_page_count - low_pages);
    }
    reserve_range(LOW_MEMORY_RESERVATION, reserved_end);
    return 1;
}

int xscore_pmm_initialize(const struct akaos_boot_info *boot_info,
                          struct xscore_physical_memory_stats *stats) {
    if (!boot_info || !stats || boot_info->memory_descriptor_size < sizeof(struct efi_memory_descriptor)) return 0;

    pmm_locked = 1;

    uint64_t highest_address = 0;
    uint64_t conventional_page_count = 0;
    for (uint64_t offset = 0; offset + sizeof(struct efi_memory_descriptor) <= boot_info->memory_map_size; offset += boot_info->memory_descriptor_size) {
        const struct efi_memory_descriptor *descriptor =
            (const void *)(uintptr_t)(boot_info->memory_map + offset);
        if (descriptor->type != EFI_CONVENTIONAL_MEMORY) continue;
        if (descriptor->number_of_pages > (UINT64_MAX - descriptor->physical_start) / PAGE_SIZE) continue;
        uint64_t end = descriptor->physical_start + descriptor->number_of_pages * PAGE_SIZE;
        uint64_t capped_end = end > MAX_PHYSICAL_MEMORY ? MAX_PHYSICAL_MEMORY : end;
        if (descriptor->physical_start < capped_end) {
            conventional_page_count += (capped_end - descriptor->physical_start) / PAGE_SIZE;
        }
        if (end > highest_address) highest_address = end;
    }
    if (highest_address > MAX_PHYSICAL_MEMORY) highest_address = MAX_PHYSICAL_MEMORY;
    managed_page_count = highest_address / PAGE_SIZE;
    if (managed_page_count == 0) return 0;
    refcount_initialize();

    for (uint64_t word = 0; word < BITMAP_WORD_COUNT; ++word) allocation_bitmap[word] = UINT64_MAX;
    free_page_count = 0;

    for (uint64_t offset = 0; offset + sizeof(struct efi_memory_descriptor) <= boot_info->memory_map_size; offset += boot_info->memory_descriptor_size) {
        const struct efi_memory_descriptor *descriptor =
            (const void *)(uintptr_t)(boot_info->memory_map + offset);
        if (descriptor->type == EFI_CONVENTIONAL_MEMORY) {
            mark_range_free(descriptor->physical_start, descriptor->number_of_pages);
        }
    }

    for (uint64_t address = 0; address < LOW_MEMORY_RESERVATION; address += PAGE_SIZE) {
        reserve_page(address / PAGE_SIZE);
    }
    /* BIOS E820 marks the loaded kernel region as available. UEFI normally
     * marks it as loader-owned, but reserving it here makes both handoffs use
     * the same safe ownership rule. */
    reserve_range((uint64_t)(uintptr_t)__text_start,
                  (uint64_t)(uintptr_t)__kernel_end);
    next_search_page = LOW_MEMORY_RESERVATION / PAGE_SIZE;
    stats->managed_pages = conventional_page_count;
    stats->free_pages = free_page_count;
    stats->highest_address = highest_address;
    return free_page_count != 0;
}

#define REFCOUNT_TABLE_SLOTS 131072U

/* Physical addresses are not guaranteed to start at zero (the native
 * ARM/RISC-V ports allocate RAM at 0x40000000). A direct page-number array
 * therefore either wastes a large low-address hole or silently loses counts.
 * Keep a bounded open-addressing table keyed by the full physical page number;
 * zero counts remain occupied so lookups preserve probe chains and can be
 * reused by later allocations. The table is sized beyond the fixed
 * process-table worst case on the six ports. */
struct page_ref_entry {
    uint64_t page;
    uint16_t count;
};

static struct page_ref_entry page_ref_table[REFCOUNT_TABLE_SLOTS];

static uint32_t get_effective_refcount_slots(void) {
    uint64_t pages = managed_page_count;
    if (pages == 0) return 4096U;
    if (pages <= 4096U) return 4096U;       /* <= 16 MB RAM: 4096 slots (64 KB) */
    if (pages <= 8192U) return 8192U;       /* <= 32 MB RAM: 8192 slots (128 KB) */
    if (pages <= 16384U) return 16384U;     /* <= 64 MB RAM: 16384 slots (256 KB) */
    return REFCOUNT_TABLE_SLOTS;            /* > 64 MB RAM: 131072 slots */
}

static void refcount_initialize(void) {
    uint32_t limit = get_effective_refcount_slots();
    for (uint32_t index = 0; index < limit; ++index) {
        page_ref_table[index].page = UINT64_MAX;
        page_ref_table[index].count = 0;
    }
}

static struct page_ref_entry *refcount_entry(uint64_t page, int create) {
    uint32_t slots = get_effective_refcount_slots();
    uint32_t start = (uint32_t)((page * UINT64_C(11400714819323198485)) /
                                (UINT64_MAX / slots + 1));
    start %= slots;
    struct page_ref_entry *reusable = 0;
    for (uint32_t step = 0; step < slots; ++step) {
        struct page_ref_entry *entry =
            &page_ref_table[(start + step) % slots];
        if (entry->page == page) return entry;
        if (entry->count == 0 && !reusable) reusable = entry;
        if (entry->page == UINT64_MAX) {
            if (!create) return 0;
            if (!reusable) reusable = entry;
            reusable->page = page;
            return reusable;
        }
    }
    if (create) {
        return reusable;
    }
    return 0;
}

#if defined(__i386__)
static void pmm_debug_hex32(uint32_t value) {
    static const char digits[] = "0123456789abcdef";
    char digit[2] = {0, 0};
    for (int shift = 28; shift >= 0; shift -= 4) {
        digit[0] = digits[(value >> shift) & 0xfU];
        xscore_serial_write(digit);
    }
}

static int pmm_is_protected_stack_page(uint64_t physical_address) {
    if (!xscore_stack_owns_physical_page(physical_address)) return 0;
    xscore_serial_write(
        "[FATAL] PMM found a free x86_32 kernel stack page; caller=0x");
    pmm_debug_hex32((uint32_t)(uintptr_t)__builtin_return_address(0));
    xscore_serial_write("\n");
    return 1;
}
#endif

uint64_t xscore_pmm_allocate_page(void) {
    uintptr_t flags = 0;
    if (pmm_locked) flags = xscore_spin_lock_irqsave(&pmm_lock);
    uint64_t start_word = next_search_page / 64;
    for (uint64_t word = start_word; word < BITMAP_WORD_COUNT; ++word) {
        uint64_t free_bits = ~allocation_bitmap[word];
        while (free_bits != 0) {
            uint64_t bit = (uint64_t)__builtin_ctzll(free_bits);
            uint64_t page = word * 64 + bit;
#if defined(__i386__)
            if (pmm_is_protected_stack_page(page * PAGE_SIZE)) {
                free_bits &= ~(UINT64_C(1) << bit);
                continue;
            }
#endif
            mark_page_used(page);
            --free_page_count;
            next_search_page = page + 1;
            struct page_ref_entry *entry = refcount_entry(page, 1);
            if (entry) entry->count = 1;
            if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
            return page * PAGE_SIZE;
        }
    }
    for (uint64_t word = 0; word < start_word; ++word) {
        uint64_t free_bits = ~allocation_bitmap[word];
        while (free_bits != 0) {
            uint64_t bit = (uint64_t)__builtin_ctzll(free_bits);
            uint64_t page = word * 64 + bit;
#if defined(__i386__)
            if (pmm_is_protected_stack_page(page * PAGE_SIZE)) {
                free_bits &= ~(UINT64_C(1) << bit);
                continue;
            }
#endif
            mark_page_used(page);
            --free_page_count;
            next_search_page = page + 1;
            struct page_ref_entry *entry = refcount_entry(page, 1);
            if (entry) entry->count = 1;
            if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
            return page * PAGE_SIZE;
        }
    }
    if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
    return 0;
}

int xscore_pmm_free_page(uint64_t physical_address) {
    if ((physical_address & (PAGE_SIZE - 1)) != 0 || physical_address < LOW_MEMORY_RESERVATION) return 0;
#if defined(__i386__)
    /* These physical pages remain kernel-owned even while a process page
     * directory is active or a virtual mapping is being changed. */
    if (xscore_stack_owns_physical_page(physical_address)) {
        xscore_serial_write(
            "[FATAL] PMM refused to free an x86_32 kernel stack page; caller=0x");
        uint32_t caller = (uint32_t)(uintptr_t)__builtin_return_address(0);
        pmm_debug_hex32(caller);
        xscore_serial_write("\n");
        return 0;
    }
#endif
    uintptr_t flags = 0;
    if (pmm_locked) flags = xscore_spin_lock_irqsave(&pmm_lock);
    uint64_t page = physical_address / PAGE_SIZE;
    if (page >= managed_page_count || !page_is_used(page)) {
        if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
        return 0;
    }
    struct page_ref_entry *entry = refcount_entry(page, 0);
    if (entry) entry->count = 0;
    mark_page_free(page);
    ++free_page_count;
    if (page < next_search_page) next_search_page = page;
    if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
    return 1;
}

void xscore_pmm_refcount_inc(uint64_t physical_address) {
    uintptr_t flags = 0;
    if (pmm_locked) flags = xscore_spin_lock_irqsave(&pmm_lock);
    uint64_t page = physical_address / PAGE_SIZE;
    struct page_ref_entry *entry = refcount_entry(page, 1);
    if (entry && entry->count != UINT16_MAX) entry->count++;
    if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
}

int xscore_pmm_refcount_dec(uint64_t physical_address) {
    uintptr_t flags = 0;
    if (pmm_locked) flags = xscore_spin_lock_irqsave(&pmm_lock);
    uint64_t page = physical_address / PAGE_SIZE;
    struct page_ref_entry *entry = refcount_entry(page, 0);
    if (entry && entry->count > 1) {
        entry->count--;
        if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
        return 1;
    }
    if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
    return xscore_pmm_free_page(physical_address);
}

uint16_t xscore_pmm_refcount_get(uint64_t physical_address) {
    uintptr_t flags = 0;
    if (pmm_locked) flags = xscore_spin_lock_irqsave(&pmm_lock);
    struct page_ref_entry *entry = refcount_entry(physical_address / PAGE_SIZE, 0);
    uint16_t count = entry && entry->count ? entry->count : 1;
    if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
    return count;
}

uint64_t xscore_pmm_free_page_count(void) {
    uintptr_t flags = 0;
    if (pmm_locked) flags = xscore_spin_lock_irqsave(&pmm_lock);
    uint64_t count = free_page_count;
    if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
    return count;
}

uint64_t xscore_pmm_total_page_count(void) {
    uintptr_t flags = 0;
    if (pmm_locked) flags = xscore_spin_lock_irqsave(&pmm_lock);
    uint64_t count = managed_page_count;
    if (pmm_locked) xscore_spin_unlock_irqrestore(&pmm_lock, flags);
    return count;
}
