#ifndef XSCORE_VIRTIO_BLK_H
#define XSCORE_VIRTIO_BLK_H

#include <stdint.h>

/* A small polled block-read interface for the first storage milestone. */
int xscore_virtio_blk_initialize(void);
int xscore_virtio_blk_present(void);
uint64_t xscore_virtio_blk_sector_count(void);
int xscore_virtio_blk_read_sector(uint64_t sector, void *buffer);
int xscore_virtio_blk_write_sector(uint64_t sector, const void *buffer);

#endif
