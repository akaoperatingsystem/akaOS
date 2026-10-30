#ifndef XSCORE_BUFCACHE_H
#define XSCORE_BUFCACHE_H

#include <stddef.h>
#include <stdint.h>

#define XSCORE_BUF_SECTOR_SIZE 512
#define XSCORE_BUF_CACHE_SECTORS 64

int xscore_bufcache_initialize(void);
int xscore_bufcache_read(uint64_t sector, void *buffer);
int xscore_bufcache_write(uint64_t sector, const void *buffer);
int xscore_bufcache_sync(void);
void xscore_bufcache_invalidate(void);

#endif
