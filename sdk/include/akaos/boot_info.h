#ifndef AKAOS_BOOT_INFO_H
#define AKAOS_BOOT_INFO_H

#include <stdint.h>

#define AKAOS_BOOT_INFO_MAGIC UINT64_C(0x534F414B414F5342)
#define AKAOS_BOOT_INFO_VERSION 1U
#define AKAOS_BOOT_FLAG_TEST_MODE (UINT64_C(1) << 0)
#define AKAOS_BOOT_FLAG_VERBOSE   (UINT64_C(1) << 1)
#define AKAOS_BOOT_FLAG_BIOS      (UINT64_C(1) << 2)

enum akaos_pixel_format {
    AKAOS_PIXEL_RGBX8888 = 0,
    AKAOS_PIXEL_BGRX8888 = 1,
    AKAOS_PIXEL_BITMASK = 2,
    AKAOS_PIXEL_BLT_ONLY = 3,
};

struct akaos_framebuffer {
    uint64_t address;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t pixel_format;
};

struct akaos_boot_info {
    uint64_t magic;
    uint32_t version;
    uint32_t size;
    uint64_t flags;
    struct akaos_framebuffer framebuffer;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint32_t memory_descriptor_version;
    uint32_t reserved;
    uint64_t acpi_rsdp;
};

#endif
