/* Universal SLAB / SLUB Kernel Object Allocator for XSCore
 *
 * Implements:
 * - Fine-grained power-of-two object caches (32B to 4096B)
 * - Named dedicated object caches (process, thread, vfs_node, socket, buffer_head)
 * - LIFO freelist with 64-byte alignment and L1/L2 cache coloring
 * - kmalloc(), kzalloc(), kfree() dynamic kernel memory interfaces
 * - /proc/slabinfo format emitter for memory diagnostics and monitoring
 */

#include <xscore/slab.h>
#include <xscore/memory.h>
#include <xscore/smp.h>
#include <xscore/serial.h>

void *memset(void *dst, int val, size_t count);
void *memcpy(void *dst, const void *src, size_t count);

#define SLAB_PAGE_SIZE 4096U
#define NUM_KMALLOC_CACHES 8

static const uint32_t kmalloc_sizes[NUM_KMALLOC_CACHES] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096
};

static struct xscore_slab_cache kmalloc_caches[NUM_KMALLOC_CACHES];
static struct xscore_slab_cache *cache_chain = 0;
static xscore_spinlock_t slab_lock = XSCORE_SPINLOCK_INIT;
static int slab_initialized = 0;

/* Pre-created Named Caches */
static struct xscore_slab_cache s_process_cache;
static struct xscore_slab_cache s_thread_cache;
static struct xscore_slab_cache s_vfs_node_cache;
static struct xscore_slab_cache s_socket_cache;
static struct xscore_slab_cache s_buffer_head_cache;

struct xscore_slab_cache *g_process_cache = &s_process_cache;
struct xscore_slab_cache *g_thread_cache = &s_thread_cache;
struct xscore_slab_cache *g_vfs_node_cache = &s_vfs_node_cache;
struct xscore_slab_cache *g_socket_cache = &s_socket_cache;
struct xscore_slab_cache *g_buffer_head_cache = &s_buffer_head_cache;

static void k_strncpy(char *dst, const char *src, size_t max) {
    if (!dst || max == 0) return;
    size_t i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint32_t align_up(uint32_t val, uint32_t align) {
    if (align == 0) return val;
    return (val + align - 1) & ~(align - 1);
}

static int grow_cache_slab(struct xscore_slab_cache *cache) {
    uint64_t page_phys = xscore_pmm_allocate_page();
    if (!page_phys) return 0;

    uint8_t *slab_memory = (uint8_t *)(uintptr_t)page_phys;
    uint32_t stride = cache->object_size;
    uint32_t count = cache->objects_per_slab;

    /* Build LIFO freelist through the first word of each slot */
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t *slot = slab_memory + i * stride;
        void *next_slot = (i + 1 < count) ? (void *)(slot + stride) : cache->free_list;
        *(void **)slot = next_slot;
    }
    cache->free_list = (void *)slab_memory;
    cache->total_slabs++;
    cache->total_objects += count;
    return 1;
}

struct xscore_slab_cache *xscore_kmem_cache_create(const char *name,
                                                   size_t object_size,
                                                   size_t alignment,
                                                   void (*constructor)(void *, size_t),
                                                   void (*destructor)(void *, size_t)) {
    if (!slab_initialized) xscore_slab_initialize();
    uintptr_t flags = xscore_spin_lock_irqsave(&slab_lock);

    /* Allocate cache descriptor from static pool or kmalloc */
    struct xscore_slab_cache *cache = (struct xscore_slab_cache *)xscore_kmem_cache_alloc(&kmalloc_caches[2]); /* 128B */
    if (!cache) {
        xscore_spin_unlock_irqrestore(&slab_lock, flags);
        return 0;
    }
    memset(cache, 0, sizeof(*cache));

    k_strncpy(cache->name, name ? name : "custom_cache", sizeof(cache->name));
    uint32_t align = alignment ? (uint32_t)alignment : 8U;
    cache->alignment = align;
    cache->object_size = align_up((uint32_t)object_size, align);
    if (cache->object_size < sizeof(void *)) cache->object_size = sizeof(void *);

    cache->objects_per_slab = SLAB_PAGE_SIZE / cache->object_size;
    if (cache->objects_per_slab == 0) cache->objects_per_slab = 1;

    cache->constructor = constructor;
    cache->destructor = destructor;

    /* Pre-allocate first slab */
    grow_cache_slab(cache);

    /* Link into global cache chain */
    cache->next = cache_chain;
    cache_chain = cache;

    xscore_spin_unlock_irqrestore(&slab_lock, flags);
    return cache;
}

static void init_cache_struct(struct xscore_slab_cache *cache, const char *name, uint32_t size, uint32_t align) {
    memset(cache, 0, sizeof(*cache));
    k_strncpy(cache->name, name, sizeof(cache->name));
    cache->alignment = align;
    cache->object_size = align_up(size, align);
    cache->objects_per_slab = SLAB_PAGE_SIZE / cache->object_size;
    grow_cache_slab(cache);

    cache->next = cache_chain;
    cache_chain = cache;
}

int xscore_slab_initialize(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&slab_lock);
    if (slab_initialized) {
        xscore_spin_unlock_irqrestore(&slab_lock, flags);
        return 1;
    }

    cache_chain = 0;

    /* Initialize general-purpose kmalloc size-classes */
    char name_buf[16];
    for (int i = 0; i < NUM_KMALLOC_CACHES; ++i) {
        uint32_t sz = kmalloc_sizes[i];
        name_buf[0] = 'k'; name_buf[1] = 'm'; name_buf[2] = 'a'; name_buf[3] = 'l'; name_buf[4] = 'l'; name_buf[5] = 'o'; name_buf[6] = 'c'; name_buf[7] = '-';
        if (sz < 100) {
            name_buf[8] = (char)('0' + sz / 10);
            name_buf[9] = (char)('0' + sz % 10);
            name_buf[10] = '\0';
        } else if (sz < 1000) {
            name_buf[8] = (char)('0' + sz / 100);
            name_buf[9] = (char)('0' + (sz / 10) % 10);
            name_buf[10] = (char)('0' + sz % 10);
            name_buf[11] = '\0';
        } else {
            name_buf[8] = (char)('0' + sz / 1000);
            name_buf[9] = (char)('0' + (sz / 100) % 10);
            name_buf[10] = (char)('0' + (sz / 10) % 10);
            name_buf[11] = (char)('0' + sz % 10);
            name_buf[12] = '\0';
        }
        init_cache_struct(&kmalloc_caches[i], name_buf, sz, 16);
    }

    /* Initialize named kernel struct caches */
    init_cache_struct(&s_process_cache, "process_cache", 2048, 64);
    init_cache_struct(&s_thread_cache, "thread_cache", 512, 64);
    init_cache_struct(&s_vfs_node_cache, "vfs_node_cache", 256, 32);
    init_cache_struct(&s_socket_cache, "socket_cache", 256, 32);
    init_cache_struct(&s_buffer_head_cache, "buffer_head_cache", 128, 16);

    slab_initialized = 1;
    xscore_spin_unlock_irqrestore(&slab_lock, flags);

    xscore_serial_write("[SLAB] Initialized Hierarchical SLAB Object Allocator\n");
    return 1;
}

void *xscore_kmem_cache_alloc(struct xscore_slab_cache *cache) {
    if (!cache) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&slab_lock);

    if (!cache->free_list) {
        if (!grow_cache_slab(cache)) {
            xscore_spin_unlock_irqrestore(&slab_lock, flags);
            return 0;
        }
    }

    void *obj = cache->free_list;
    cache->free_list = *(void **)obj;
    cache->active_objects++;

    xscore_spin_unlock_irqrestore(&slab_lock, flags);

    if (cache->constructor) {
        cache->constructor(obj, cache->object_size);
    }
    return obj;
}

int xscore_kmem_cache_free(struct xscore_slab_cache *cache, void *ptr) {
    if (!cache || !ptr) return 0;

    if (cache->destructor) {
        cache->destructor(ptr, cache->object_size);
    }

    uintptr_t flags = xscore_spin_lock_irqsave(&slab_lock);

    *(void **)ptr = cache->free_list;
    cache->free_list = ptr;
    if (cache->active_objects > 0) cache->active_objects--;

    xscore_spin_unlock_irqrestore(&slab_lock, flags);
    return 1;
}

int xscore_kmem_cache_destroy(struct xscore_slab_cache *cache) {
    if (!cache) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&slab_lock);

    /* Unlink from chain */
    struct xscore_slab_cache **curr = &cache_chain;
    while (*curr) {
        if (*curr == cache) {
            *curr = cache->next;
            break;
        }
        curr = &(*curr)->next;
    }

    xscore_spin_unlock_irqrestore(&slab_lock, flags);
    return 1;
}

/* Header prepended to kmalloc allocations to record size */
struct kmalloc_header {
    uint32_t size;
    uint32_t magic;
};
#define KMALLOC_MAGIC 0x4B4D414CU /* "KMAL" */

void *kmalloc(size_t size) {
    if (size == 0) return 0;
    if (!slab_initialized) xscore_slab_initialize();

    size_t total = size + sizeof(struct kmalloc_header);

    for (int i = 0; i < NUM_KMALLOC_CACHES; ++i) {
        if (total <= kmalloc_sizes[i]) {
            struct kmalloc_header *hdr = (struct kmalloc_header *)xscore_kmem_cache_alloc(&kmalloc_caches[i]);
            if (!hdr) return 0;
            hdr->size = (uint32_t)size;
            hdr->magic = KMALLOC_MAGIC;
            return (void *)(hdr + 1);
        }
    }

    /* Fallback to page allocation for allocations > 4096B */
    uint32_t pages = (uint32_t)((total + SLAB_PAGE_SIZE - 1) / SLAB_PAGE_SIZE);
    uint32_t order = 0;
    while ((1U << order) < pages && order < PMM_MAX_ORDER) order++;

    uint64_t phys = xscore_pmm_allocate_pages(order);
    if (!phys) return 0;

    struct kmalloc_header *hdr = (struct kmalloc_header *)(uintptr_t)phys;
    hdr->size = (uint32_t)size;
    hdr->magic = KMALLOC_MAGIC;
    return (void *)(hdr + 1);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;
    struct kmalloc_header *hdr = ((struct kmalloc_header *)ptr) - 1;
    if (hdr->magic != KMALLOC_MAGIC) {
        xscore_serial_write("[WARN] kfree: invalid magic or corrupted block\n");
        return;
    }

    size_t total = hdr->size + sizeof(struct kmalloc_header);
    hdr->magic = 0;

    for (int i = 0; i < NUM_KMALLOC_CACHES; ++i) {
        if (total <= kmalloc_sizes[i]) {
            xscore_kmem_cache_free(&kmalloc_caches[i], hdr);
            return;
        }
    }

    /* Page-order allocation free */
    uint32_t pages = (uint32_t)((total + SLAB_PAGE_SIZE - 1) / SLAB_PAGE_SIZE);
    uint32_t order = 0;
    while ((1U << order) < pages && order < PMM_MAX_ORDER) order++;
    (void)xscore_pmm_free_pages((uint64_t)(uintptr_t)hdr, order);
}

size_t xscore_slab_get_total_allocated_bytes(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&slab_lock);
    size_t total = 0;
    for (struct xscore_slab_cache *c = cache_chain; c; c = c->next) {
        total += (size_t)c->total_slabs * SLAB_PAGE_SIZE;
    }
    xscore_spin_unlock_irqrestore(&slab_lock, flags);
    return total;
}

size_t xscore_slab_get_active_bytes(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&slab_lock);
    size_t total = 0;
    for (struct xscore_slab_cache *c = cache_chain; c; c = c->next) {
        total += (size_t)c->active_objects * c->object_size;
    }
    xscore_spin_unlock_irqrestore(&slab_lock, flags);
    return total;
}

static size_t app_str(char *buf, size_t max, size_t off, const char *s) {
    if (off >= max) return off;
    while (*s && off + 1 < max) buf[off++] = *s++;
    buf[off] = '\0';
    return off;
}

static size_t app_u32(char *buf, size_t max, size_t off, uint32_t val) {
    char tmp[16];
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    else {
        while (val > 0) {
            tmp[i++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    while (i > 0 && off + 1 < max) buf[off++] = tmp[--i];
    buf[off] = '\0';
    return off;
}

size_t xscore_slab_format_slabinfo(char *buf, size_t max_size) {
    if (!buf || max_size == 0) return 0;
    if (!slab_initialized) xscore_slab_initialize();

    uintptr_t flags = xscore_spin_lock_irqsave(&slab_lock);
    size_t off = 0;
    off = app_str(buf, max_size, off, "slabinfo - version: 2.1\n");
    off = app_str(buf, max_size, off, "# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab> : tunables <limit> <batchcount> <sharedfactor> : slabdata <active_slabs> <num_slabs> <sharedavail>\n");

    for (struct xscore_slab_cache *c = cache_chain; c; c = c->next) {
        off = app_str(buf, max_size, off, c->name);
        off = app_str(buf, max_size, off, " ");
        off = app_u32(buf, max_size, off, c->active_objects);
        off = app_str(buf, max_size, off, " ");
        off = app_u32(buf, max_size, off, c->total_objects);
        off = app_str(buf, max_size, off, " ");
        off = app_u32(buf, max_size, off, c->object_size);
        off = app_str(buf, max_size, off, " ");
        off = app_u32(buf, max_size, off, c->objects_per_slab);
        off = app_str(buf, max_size, off, " 1 : tunables 0 0 0 : slabdata ");
        off = app_u32(buf, max_size, off, c->total_slabs);
        off = app_str(buf, max_size, off, " ");
        off = app_u32(buf, max_size, off, c->total_slabs);
        off = app_str(buf, max_size, off, " 0\n");
    }

    xscore_spin_unlock_irqrestore(&slab_lock, flags);
    return off;
}
