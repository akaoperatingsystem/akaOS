#ifndef XSCORE_INITRAMFS_H
#define XSCORE_INITRAMFS_H

#include <stddef.h>
#include <stdint.h>

int xscore_initramfs_initialize(void);
int xscore_initramfs_mount_archive(const uint8_t *archive, size_t archive_size);

#endif
