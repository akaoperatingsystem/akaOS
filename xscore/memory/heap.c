#include <xscore/heap.h>
#include <xscore/memory.h>
#include <xscore/smp.h>
#include <xscore/vmm.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE UINT64_C(4096)
#if defined(__i386__)
#define HEAP_BASE UINT64_C(0x10000000)
#elif defined(__arm__) || (defined(__riscv) && __riscv_xlen == 32)
#define HEAP_BASE UINT64_C(0x30000000)
#elif (defined(__riscv) && __riscv_xlen == 64)
#define HEAP_BASE UINT64_C(0xffffffc000000000)
#else
#define HEAP_BASE UINT64_C(0xffff800000000000)
#endif
#define HEAP_GROWTH_PAGES UINT64_C(16)
#if defined(__x86_64__)
/* A process record includes bounded mmap-page metadata.  Keep enough kernel
 * heap mapped from boot for one exec staging record so exec tests and normal
 * process replacement do not grow the monotonic heap after the reclamation
 * baseline is sampled. */
#define HEAP_INITIAL_PAGES UINT64_C(256)
#else
#define HEAP_INITIAL_PAGES HEAP_GROWTH_PAGES
#endif
#define HEAP_MAGIC UINT32_C(0x58484d42)

struct heap_block {
    uint64_t size;
    struct heap_block *next;
    uint32_t magic;
    uint8_t free;
    uint8_t reserved[3];
};

static struct heap_block *first_block;
static uint64_t mapped_bytes;
static uint64_t allocated_bytes;
static uint64_t allocation_count;
/* SMP safety: freelist splits/coalesces and growth mutate the same chain.
 * The lock deliberately remains held while mapping growth pages: dropping it
 * after a failed freelist search lets two CPUs grow at the same virtual base. */
static xscore_spinlock_t heap_lock = XSCORE_SPINLOCK_INIT;
static int heap_locked;

static uint64_t align_16(uint64_t size) {
    return (size + 15) & ~UINT64_C(15);
}

static struct heap_block *last_block(void) {
    struct heap_block *block = first_block;
    if (!block) return 0;
    while (block->next) block = block->next;
    return block;
}

static int grow_heap(uint64_t minimum_payload) {
    uint64_t required = minimum_payload + sizeof(struct heap_block);
    uint64_t page_count = (required + PAGE_SIZE - 1) / PAGE_SIZE;
    if (page_count < HEAP_GROWTH_PAGES) page_count = HEAP_GROWTH_PAGES;
    uint64_t growth_start = HEAP_BASE + mapped_bytes;

    for (uint64_t page = 0; page < page_count; ++page) {
        uint64_t physical = xscore_pmm_allocate_page();
        if (!physical) {
            for (uint64_t rollback = 0; rollback < page; ++rollback) {
                uint64_t rollback_physical = 0;
                xscore_vmm_unmap_page(growth_start + rollback * PAGE_SIZE, &rollback_physical);
                xscore_pmm_free_page(rollback_physical);
            }
            return 0;
        }
        uint64_t virtual_address = growth_start + page * PAGE_SIZE;
        /* Heap is pure data: writable, never executable (NX valid because
         * xscore_vmm_initialize enabled EFER.NXE before mapping anything). */
        if (!xscore_vmm_map_page(virtual_address, physical,
                                 XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE)) {
            xscore_pmm_free_page(physical);
            for (uint64_t rollback = 0; rollback < page; ++rollback) {
                uint64_t rollback_physical = 0;
                xscore_vmm_unmap_page(growth_start + rollback * PAGE_SIZE, &rollback_physical);
                xscore_pmm_free_page(rollback_physical);
            }
            return 0;
        }
    }

    uint64_t growth_bytes = page_count * PAGE_SIZE;
    struct heap_block *block = (struct heap_block *)(uintptr_t)growth_start;
    block->size = growth_bytes - sizeof(*block);
    block->next = 0;
    block->magic = HEAP_MAGIC;
    block->free = 1;

    struct heap_block *tail = last_block();
    if (!tail) {
        first_block = block;
    } else if (tail->free && (uint64_t)(uintptr_t)tail + sizeof(*tail) + tail->size == growth_start) {
        tail->size += growth_bytes;
    } else {
        tail->next = block;
    }
    mapped_bytes += growth_bytes;
    return 1;
}

int xscore_heap_initialize(void) {
    heap_locked = 1;
    first_block = 0;
    mapped_bytes = 0;
    allocated_bytes = 0;
    allocation_count = 0;
    return grow_heap(HEAP_INITIAL_PAGES * PAGE_SIZE - sizeof(struct heap_block));
}

void *xscore_heap_allocate(size_t requested_size) {
    if (requested_size == 0) return 0;
    uint64_t size = align_16((uint64_t)requested_size);
    if (size < requested_size) return 0;

    for (;;) {
        uintptr_t lock_flags = 0;
        if (heap_locked) lock_flags = xscore_spin_lock_irqsave(&heap_lock);
        for (struct heap_block *block = first_block; block; block = block->next) {
            if (block->magic != HEAP_MAGIC || !block->free || block->size < size) continue;
            uint64_t remaining = block->size - size;
            if (remaining >= sizeof(struct heap_block) + 16) {
                struct heap_block *split = (void *)((uint8_t *)(block + 1) + size);
                split->size = remaining - sizeof(*split);
                split->next = block->next;
                split->magic = HEAP_MAGIC;
                split->free = 1;
                block->next = split;
                block->size = size;
            }
            block->free = 0;
            allocated_bytes += block->size;
            ++allocation_count;
            if (heap_locked) xscore_spin_unlock_irqrestore(&heap_lock, lock_flags);
            return block + 1;
        }
        if (!grow_heap(size)) {
            if (heap_locked) xscore_spin_unlock_irqrestore(&heap_lock, lock_flags);
            return 0;
        }
        if (heap_locked) xscore_spin_unlock_irqrestore(&heap_lock, lock_flags);
    }
}

int xscore_heap_free(void *pointer) {
    if (!pointer) return 0;
    uintptr_t lock_flags = 0;
    if (heap_locked) lock_flags = xscore_spin_lock_irqsave(&heap_lock);
    uint64_t address = (uint64_t)(uintptr_t)pointer;
    if (address < HEAP_BASE + sizeof(struct heap_block) || address >= HEAP_BASE + mapped_bytes) {
        if (heap_locked) xscore_spin_unlock_irqrestore(&heap_lock, lock_flags);
        return 0;
    }
    struct heap_block *block = (struct heap_block *)pointer - 1;
    if (block->magic != HEAP_MAGIC || block->free) {
        if (heap_locked) xscore_spin_unlock_irqrestore(&heap_lock, lock_flags);
        return 0;
    }
    block->free = 1;
    allocated_bytes -= block->size;
    --allocation_count;

    for (struct heap_block *current = first_block; current && current->next;) {
        struct heap_block *next = current->next;
        uint64_t current_end = (uint64_t)(uintptr_t)current + sizeof(*current) + current->size;
        if (current->free && next->free && current_end == (uint64_t)(uintptr_t)next) {
            current->size += sizeof(*next) + next->size;
            current->next = next->next;
            continue;
        }
        current = current->next;
    }
    if (heap_locked) xscore_spin_unlock_irqrestore(&heap_lock, lock_flags);
    return 1;
}

void xscore_heap_get_stats(struct xscore_heap_stats *stats) {
    if (!stats) return;
    uintptr_t lock_flags = 0;
    if (heap_locked) lock_flags = xscore_spin_lock_irqsave(&heap_lock);
    stats->mapped_bytes = mapped_bytes;
    stats->allocated_bytes = allocated_bytes;
    stats->allocation_count = allocation_count;
    if (heap_locked) xscore_spin_unlock_irqrestore(&heap_lock, lock_flags);
}
