/* ============================================================
 * akaOS — Multiboot2 Information Structures
 * ============================================================ */

#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include <stdint.h>

#define MB2_TAG_END         0
#define MB2_TAG_CMDLINE     1
#define MB2_TAG_BOOTLOADER  2
#define MB2_TAG_BASIC_MEMINFO 4
#define MB2_TAG_FRAMEBUFFER 8

struct mb2_info {
    uint32_t total_size;
    uint32_t reserved;
};

struct mb2_tag {
    uint32_t type;
    uint32_t size;
};

struct mb2_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

struct mb2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  reserved;
} __attribute__((packed));

/* Find a tag by type in multiboot2 info. Returns NULL if not found. */
static inline struct mb2_tag *mb2_find_tag(uint64_t mb2_addr, uint32_t tag_type) {
    struct mb2_info *info = (struct mb2_info *)(uintptr_t)mb2_addr;
    struct mb2_tag *tag = (struct mb2_tag *)((uintptr_t)mb2_addr + 8);
    while ((uintptr_t)tag < (uintptr_t)mb2_addr + info->total_size) {
        if (tag->type == tag_type) return tag;
        if (tag->type == MB2_TAG_END) break;
        uintptr_t next = (uintptr_t)tag + ((tag->size + 7) & ~7);
        tag = (struct mb2_tag *)next;
    }
    return 0;
}

#endif
