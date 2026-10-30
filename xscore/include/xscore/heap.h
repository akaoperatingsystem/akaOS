#ifndef XSCORE_HEAP_H
#define XSCORE_HEAP_H

#include <stddef.h>
#include <stdint.h>

struct xscore_heap_stats {
    uint64_t mapped_bytes;
    uint64_t allocated_bytes;
    uint64_t allocation_count;
};

int xscore_heap_initialize(void);
void *xscore_heap_allocate(size_t size);
int xscore_heap_free(void *pointer);
void xscore_heap_get_stats(struct xscore_heap_stats *stats);

#endif

