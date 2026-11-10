#ifndef XSCORE_SLAB_H
#define XSCORE_SLAB_H

#include <stddef.h>
#include <stdint.h>

#define SLAB_NAME_MAX 32
#define SLAB_MIN_SIZE 32
#define SLAB_MAX_SIZE 4096

/* Object Cache Structure */
struct xscore_slab_cache {
    char name[SLAB_NAME_MAX];
    uint32_t object_size;       /* Size of each object in bytes */
    uint32_t alignment;         /* Alignment requirement (typically 8, 16, or 64) */
    uint32_t objects_per_slab;  /* Number of objects packed into a 4KB slab page */
    uint32_t total_slabs;       /* Total number of allocated slab pages */
    uint32_t active_objects;    /* Currently allocated object count */
    uint32_t total_objects;     /* Total capacity of objects across all slabs */
    void *free_list;            /* LIFO freelist of available object slots */
    void (*constructor)(void *object, size_t size);
    void (*destructor)(void *object, size_t size);
    struct xscore_slab_cache *next;
};

/* Core SLAB / SLUB Cache Management */
int xscore_slab_initialize(void);
struct xscore_slab_cache *xscore_kmem_cache_create(const char *name,
                                                   size_t object_size,
                                                   size_t alignment,
                                                   void (*constructor)(void *, size_t),
                                                   void (*destructor)(void *, size_t));
void *xscore_kmem_cache_alloc(struct xscore_slab_cache *cache);
int xscore_kmem_cache_free(struct xscore_slab_cache *cache, void *ptr);
int xscore_kmem_cache_destroy(struct xscore_slab_cache *cache);

/* General-purpose Kernel Dynamic Allocator */
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *ptr);

/* Pre-created Named Object Caches for High-Frequency Kernel Structs */
extern struct xscore_slab_cache *g_process_cache;
extern struct xscore_slab_cache *g_thread_cache;
extern struct xscore_slab_cache *g_vfs_node_cache;
extern struct xscore_slab_cache *g_socket_cache;
extern struct xscore_slab_cache *g_buffer_head_cache;

/* Slab Statistics */
size_t xscore_slab_get_total_allocated_bytes(void);
size_t xscore_slab_get_active_bytes(void);
size_t xscore_slab_format_slabinfo(char *buf, size_t max_size);

#endif /* XSCORE_SLAB_H */
