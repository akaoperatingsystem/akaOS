#include <xscore/page_cache.h>
#include <xscore/memory.h>
#include <xscore/serial.h>
#include <xscore/smp.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_CACHE_CAPACITY 256U

struct page_cache_entry {
    const uint8_t *image;
    uint64_t offset;
    uint64_t physical_address;
};

static struct page_cache_entry cache_entries[PAGE_CACHE_CAPACITY];
static uint32_t cache_entry_count = 0;
static uint32_t cache_hits = 0;
static uint32_t cache_misses = 0;
static xscore_spinlock_t page_cache_lock = XSCORE_SPINLOCK_INIT;

int xscore_page_cache_initialize(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&page_cache_lock);
    for (uint32_t i = 0; i < PAGE_CACHE_CAPACITY; ++i) {
        cache_entries[i].image = 0;
        cache_entries[i].offset = 0;
        cache_entries[i].physical_address = 0;
    }
    cache_entry_count = 0;
    cache_hits = 0;
    cache_misses = 0;
    xscore_spin_unlock_irqrestore(&page_cache_lock, flags);
    return 1;
}

uint64_t xscore_page_cache_lookup(const uint8_t *image, uint64_t offset) {
    if (!image) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&page_cache_lock);
    for (uint32_t i = 0; i < PAGE_CACHE_CAPACITY; ++i) {
        if (cache_entries[i].image == image &&
            cache_entries[i].offset == offset &&
            cache_entries[i].physical_address != 0) {
            cache_hits++;
            uint64_t physical = cache_entries[i].physical_address;
            xscore_spin_unlock_irqrestore(&page_cache_lock, flags);
            return physical;
        }
    }
    cache_misses++;
    xscore_spin_unlock_irqrestore(&page_cache_lock, flags);
    return 0;
}

int xscore_page_cache_insert(const uint8_t *image, uint64_t offset, uint64_t physical_address) {
    if (!image || !physical_address) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&page_cache_lock);

    /* Check if already present */
    for (uint32_t i = 0; i < PAGE_CACHE_CAPACITY; ++i) {
        if (cache_entries[i].image == image &&
            cache_entries[i].offset == offset) {
            if (cache_entries[i].physical_address != physical_address) {
                (void)xscore_pmm_refcount_dec(cache_entries[i].physical_address);
                xscore_pmm_refcount_inc(physical_address);
                cache_entries[i].physical_address = physical_address;
            }
            xscore_spin_unlock_irqrestore(&page_cache_lock, flags);
            return 1;
        }
    }

    /* Find empty slot */
    for (uint32_t i = 0; i < PAGE_CACHE_CAPACITY; ++i) {
        if (cache_entries[i].physical_address == 0) {
            cache_entries[i].image = image;
            cache_entries[i].offset = offset;
            cache_entries[i].physical_address = physical_address;
            xscore_pmm_refcount_inc(physical_address);
            cache_entry_count++;
            xscore_spin_unlock_irqrestore(&page_cache_lock, flags);
            return 1;
        }
    }

    /* If full, evict slot 0 */
    if (cache_entries[0].physical_address) {
        (void)xscore_pmm_refcount_dec(cache_entries[0].physical_address);
    }
    for (uint32_t i = 1; i < PAGE_CACHE_CAPACITY; ++i) {
        cache_entries[i - 1] = cache_entries[i];
    }
    cache_entries[PAGE_CACHE_CAPACITY - 1].image = image;
    cache_entries[PAGE_CACHE_CAPACITY - 1].offset = offset;
    cache_entries[PAGE_CACHE_CAPACITY - 1].physical_address = physical_address;
    xscore_pmm_refcount_inc(physical_address);
    xscore_spin_unlock_irqrestore(&page_cache_lock, flags);
    return 1;
}

void xscore_page_cache_evict(uint64_t physical_address) {
    if (!physical_address) return;
    uintptr_t flags = xscore_spin_lock_irqsave(&page_cache_lock);
    for (uint32_t i = 0; i < PAGE_CACHE_CAPACITY; ++i) {
        if (cache_entries[i].physical_address == physical_address) {
            uint64_t phys = cache_entries[i].physical_address;
            cache_entries[i].image = 0;
            cache_entries[i].offset = 0;
            cache_entries[i].physical_address = 0;
            if (cache_entry_count > 0) cache_entry_count--;
            (void)xscore_pmm_refcount_dec(phys);
            xscore_spin_unlock_irqrestore(&page_cache_lock, flags);
            return;
        }
    }
    xscore_spin_unlock_irqrestore(&page_cache_lock, flags);
}

void xscore_page_cache_clear(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&page_cache_lock);
    for (uint32_t i = 0; i < PAGE_CACHE_CAPACITY; ++i) {
        if (cache_entries[i].physical_address) {
            uint64_t phys = cache_entries[i].physical_address;
            cache_entries[i].image = 0;
            cache_entries[i].offset = 0;
            cache_entries[i].physical_address = 0;
            (void)xscore_pmm_refcount_dec(phys);
        }
    }
    cache_entry_count = 0;
    xscore_spin_unlock_irqrestore(&page_cache_lock, flags);
}

void xscore_page_cache_stats(uint32_t *out_entries, uint32_t *out_hits, uint32_t *out_misses) {
    uintptr_t flags = xscore_spin_lock_irqsave(&page_cache_lock);
    if (out_entries) *out_entries = cache_entry_count;
    if (out_hits) *out_hits = cache_hits;
    if (out_misses) *out_misses = cache_misses;
    xscore_spin_unlock_irqrestore(&page_cache_lock, flags);
}
