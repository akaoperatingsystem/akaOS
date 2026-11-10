#include <xscore/bufcache.h>
#include <xscore/virtio_blk.h>
#include <xscore/nvme.h>
#include <xscore/ahci.h>
#include <xscore/serial.h>
#include <stddef.h>
#include <stdint.h>

#define BUF_VALID UINT32_C(1)
#define BUF_DIRTY UINT32_C(2)

struct xscore_buf_entry {
    uint64_t sector;
    uint32_t flags;
    uint64_t access_tick;
    uint8_t data[XSCORE_BUF_SECTOR_SIZE];
};

int xscore_block_present(void) {
    return xscore_virtio_blk_present() || xscore_nvme_block_present() || xscore_ahci_present();
}

uint64_t xscore_block_sector_count(void) {
    if (xscore_virtio_blk_present()) return xscore_virtio_blk_sector_count();
    if (xscore_nvme_block_present()) return xscore_nvme_sector_count();
    if (xscore_ahci_present()) {
        const struct xscore_ahci_device *dev = xscore_ahci_get_device(0);
        if (dev) return dev->sector_count;
    }
    return 0;
}

int xscore_block_read_sector(uint64_t sector, void *buffer) {
    if (xscore_virtio_blk_present()) return xscore_virtio_blk_read_sector(sector, buffer);
    if (xscore_nvme_block_present()) return xscore_nvme_read_sector(sector, buffer);
    if (xscore_ahci_present()) {
        const struct xscore_ahci_device *dev = xscore_ahci_get_device(0);
        if (dev) return xscore_ahci_read_sectors(dev->port_num, sector, 1, buffer);
    }
    return 0;
}

int xscore_block_write_sector(uint64_t sector, const void *buffer) {
    if (xscore_virtio_blk_present()) return xscore_virtio_blk_write_sector(sector, buffer);
    if (xscore_nvme_block_present()) return xscore_nvme_write_sector(sector, buffer);
    if (xscore_ahci_present()) {
        const struct xscore_ahci_device *dev = xscore_ahci_get_device(0);
        if (dev) {
            int ok = xscore_ahci_write_sectors(dev->port_num, sector, 1, buffer);
            if (ok) ok = xscore_ahci_flush(dev->port_num);
            return ok;
        }
    }
    return 0;
}

static struct xscore_buf_entry cache[XSCORE_BUF_CACHE_SECTORS];
static uint64_t global_tick_seq = 0;
static int cache_initialized = 0;

int xscore_bufcache_initialize(void) {
    for (size_t i = 0; i < XSCORE_BUF_CACHE_SECTORS; ++i) {
        cache[i].sector = 0;
        cache[i].flags = 0;
        cache[i].access_tick = 0;
        for (size_t j = 0; j < XSCORE_BUF_SECTOR_SIZE; ++j) {
            cache[i].data[j] = 0;
        }
    }
    global_tick_seq = 0;
    cache_initialized = 1;
    return 1;
}

static struct xscore_buf_entry *find_entry(uint64_t sector) {
    for (size_t i = 0; i < XSCORE_BUF_CACHE_SECTORS; ++i) {
        if ((cache[i].flags & BUF_VALID) && cache[i].sector == sector) {
            return &cache[i];
        }
    }
    return NULL;
}

static struct xscore_buf_entry *allocate_entry(uint64_t sector) {
    /* Look for an unused slot first */
    for (size_t i = 0; i < XSCORE_BUF_CACHE_SECTORS; ++i) {
        if (!(cache[i].flags & BUF_VALID)) {
            cache[i].sector = sector;
            cache[i].flags = BUF_VALID;
            cache[i].access_tick = ++global_tick_seq;
            return &cache[i];
        }
    }

    /* Find LRU victim */
    size_t victim_idx = 0;
    uint64_t min_tick = cache[0].access_tick;
    for (size_t i = 1; i < XSCORE_BUF_CACHE_SECTORS; ++i) {
        if (cache[i].access_tick < min_tick) {
            min_tick = cache[i].access_tick;
            victim_idx = i;
        }
    }

    struct xscore_buf_entry *victim = &cache[victim_idx];
    if (victim->flags & BUF_DIRTY) {
        xscore_block_write_sector(victim->sector, victim->data);
        victim->flags &= ~BUF_DIRTY;
    }

    victim->sector = sector;
    victim->flags = BUF_VALID;
    victim->access_tick = ++global_tick_seq;
    return victim;
}

int xscore_bufcache_read(uint64_t sector, void *buffer) {
    if (!cache_initialized) {
        xscore_bufcache_initialize();
    }
    if (!buffer || !xscore_block_present() || sector >= xscore_block_sector_count()) return 0;

    struct xscore_buf_entry *entry = find_entry(sector);
    if (entry) {
        entry->access_tick = ++global_tick_seq;
        uint8_t *dst = (uint8_t *)buffer;
        for (size_t i = 0; i < XSCORE_BUF_SECTOR_SIZE; ++i) {
            dst[i] = entry->data[i];
        }
        return 1;
    }

    entry = allocate_entry(sector);
    if (!xscore_block_read_sector(sector, entry->data)) {
        entry->flags = 0;
        return 0;
    }

    uint8_t *dst = (uint8_t *)buffer;
    for (size_t i = 0; i < XSCORE_BUF_SECTOR_SIZE; ++i) {
        dst[i] = entry->data[i];
    }
    return 1;
}

int xscore_bufcache_write(uint64_t sector, const void *buffer) {
    if (!cache_initialized) {
        xscore_bufcache_initialize();
    }
    if (!buffer || !xscore_block_present() || sector >= xscore_block_sector_count()) return 0;

    struct xscore_buf_entry *entry = find_entry(sector);
    if (!entry) {
        entry = allocate_entry(sector);
    }

    const uint8_t *src = (const uint8_t *)buffer;
    for (size_t i = 0; i < XSCORE_BUF_SECTOR_SIZE; ++i) {
        entry->data[i] = src[i];
    }
    entry->flags |= (BUF_VALID | BUF_DIRTY);
    entry->access_tick = ++global_tick_seq;
    return 1;
}

int xscore_bufcache_sync(void) {
    if (!cache_initialized || !xscore_block_present()) return 0;

    for (size_t i = 0; i < XSCORE_BUF_CACHE_SECTORS; ++i) {
        if ((cache[i].flags & (BUF_VALID | BUF_DIRTY)) == (BUF_VALID | BUF_DIRTY)) {
            if (xscore_block_write_sector(cache[i].sector, cache[i].data)) {
                cache[i].flags &= ~BUF_DIRTY;
            }
        }
    }
    return 1;
}

void xscore_bufcache_invalidate(void) {
    for (size_t i = 0; i < XSCORE_BUF_CACHE_SECTORS; ++i) {
        cache[i].flags = 0;
        cache[i].sector = 0;
        cache[i].access_tick = 0;
    }
}
