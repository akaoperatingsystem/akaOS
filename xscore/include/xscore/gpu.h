#ifndef XSCORE_GPU_H
#define XSCORE_GPU_H

#include <stdint.h>
#include <stddef.h>

struct xscore_gpu_device {
    char driver_name[32];
    char chipset_name[64];
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_id;
    uint16_t class_code;
    uint64_t fb_phys_base;
    uint64_t fb_size_bytes;
    uint64_t mmio_phys_base;
    uint32_t vram_size_mb;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t stride;
    int accel_2d;
    int accel_3d;
    int ray_tracing;
    int modeset_supported;
    uint8_t pcie_gen;
    uint8_t pcie_width;
};

void xscore_gpu_initialize(void);
uint32_t xscore_gpu_count(void);
const struct xscore_gpu_device *xscore_gpu_get(uint32_t index);
const struct xscore_gpu_device *xscore_gpu_get_active(void);
int xscore_gpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
size_t xscore_gpu_format_proc_gpuinfo(char *buf, size_t max_size);

#endif

