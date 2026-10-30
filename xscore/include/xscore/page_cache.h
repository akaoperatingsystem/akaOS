#ifndef XSCORE_PAGE_CACHE_H
#define XSCORE_PAGE_CACHE_H

#include <stddef.h>
#include <stdint.h>

int xscore_page_cache_initialize(void);
uint64_t xscore_page_cache_lookup(const uint8_t *image, uint64_t offset);
int xscore_page_cache_insert(const uint8_t *image, uint64_t offset, uint64_t physical_address);
void xscore_page_cache_evict(uint64_t physical_address);
void xscore_page_cache_clear(void);
void xscore_page_cache_stats(uint32_t *out_entries, uint32_t *out_hits, uint32_t *out_misses);

#endif /* XSCORE_PAGE_CACHE_H */
