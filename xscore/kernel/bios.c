#include <akaos/boot_info.h>
#include <stdint.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC UINT32_C(0x36d76289)
#define MULTIBOOT_TAG_END 0U
#define MULTIBOOT_TAG_CMDLINE 1U
#define MULTIBOOT_TAG_MMAP 6U
#define MULTIBOOT_TAG_FRAMEBUFFER 8U
#define MULTIBOOT_MMAP_AVAILABLE 1U
#define BIOS_MEMORY_DESCRIPTOR_CAPACITY 128U

/* The PMM consumes the same fixed-width descriptor layout as the UEFI path. */
struct bios_memory_descriptor {
    uint32_t type;
    uint32_t padding;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attributes;
};

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct multiboot_mmap_entry {
    uint64_t address;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
};

struct multiboot_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_address;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
    uint8_t red_field_position;
    uint8_t red_mask_size;
    uint8_t green_field_position;
    uint8_t green_mask_size;
    uint8_t blue_field_position;
    uint8_t blue_mask_size;
} __attribute__((packed));

static struct akaos_boot_info bios_boot_info;
static struct bios_memory_descriptor bios_memory_map[BIOS_MEMORY_DESCRIPTOR_CAPACITY];
static int multiboot_uefi_handoff;

extern void xscore_entry(const struct akaos_boot_info *boot_info);
extern void xscore_serial_write(const char *text);

static uint32_t align_up_u32(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static int text_contains(const uint8_t *text, uint32_t length, const char *needle) {
    for (uint32_t start = 0; start < length; ++start) {
        uint32_t index = 0;
        while (needle[index] && start + index < length &&
               text[start + index] == (uint8_t)needle[index]) {
            ++index;
        }
        if (!needle[index]) return 1;
    }
    return 0;
}

static void consume_cmdline(const struct multiboot_tag *tag) {
    if (tag->size <= 8) return;
    const uint8_t *text = (const uint8_t *)tag + 8;
    uint32_t length = tag->size - 8;
    if (text_contains(text, length, "AKAOS.TEST") ||
        text_contains(text, length, "akaos.test")) {
        bios_boot_info.flags |= AKAOS_BOOT_FLAG_TEST_MODE;
    }
    if (text_contains(text, length, "VERBOSE") ||
        text_contains(text, length, "verbose")) {
        bios_boot_info.flags |= AKAOS_BOOT_FLAG_VERBOSE;
    }
    if (text_contains(text, length, "AKAOS.UEFI")) {
        multiboot_uefi_handoff = 1;
    }
}

static void consume_framebuffer(const struct multiboot_tag *tag) {
    if (tag->size < sizeof(struct multiboot_tag_framebuffer)) {
        xscore_serial_write("[WARN] BIOS framebuffer tag is truncated\n");
        return;
    }
    const struct multiboot_tag_framebuffer *framebuffer =
        (const struct multiboot_tag_framebuffer *)tag;
    if (framebuffer->framebuffer_type != 1 || framebuffer->framebuffer_bpp != 32 ||
        framebuffer->framebuffer_width == 0 || framebuffer->framebuffer_height == 0 ||
        framebuffer->framebuffer_pitch < framebuffer->framebuffer_width * 4U) {
        xscore_serial_write("[WARN] BIOS framebuffer mode is unavailable\n");
        return;
    }

    uint32_t pixel_format;
    if (framebuffer->red_field_position == 16 &&
        framebuffer->green_field_position == 8 &&
        framebuffer->blue_field_position == 0) {
        pixel_format = AKAOS_PIXEL_RGBX8888;
    } else if (framebuffer->red_field_position == 0 &&
               framebuffer->green_field_position == 8 &&
               framebuffer->blue_field_position == 16) {
        pixel_format = AKAOS_PIXEL_BGRX8888;
    } else {
        xscore_serial_write("[WARN] BIOS framebuffer pixel layout is unsupported\n");
        return;
    }

    /* Multiboot's direct-colour payload is translated to the same 32-bit
     * RGB/BGR layouts consumed by the XSCore canvas. */
    bios_boot_info.framebuffer.address = framebuffer->framebuffer_address;
    bios_boot_info.framebuffer.size =
        (uint64_t)framebuffer->framebuffer_pitch * framebuffer->framebuffer_height;
    bios_boot_info.framebuffer.width = framebuffer->framebuffer_width;
    bios_boot_info.framebuffer.height = framebuffer->framebuffer_height;
    bios_boot_info.framebuffer.pixels_per_scanline = framebuffer->framebuffer_pitch / 4U;
    bios_boot_info.framebuffer.pixel_format = pixel_format;
    xscore_serial_write("[INFO] BIOS framebuffer tag accepted\n");
}

static int build_bios_boot_info(uint64_t multiboot_info_address) {
    multiboot_uefi_handoff = 0;
    const uint8_t *base = (const uint8_t *)(uintptr_t)multiboot_info_address;
    uint32_t total_size = *(const uint32_t *)base;
    if (total_size < 16) return 0;

    uint32_t descriptor_count = 0;
    const uint8_t *end = base + total_size;
    const struct multiboot_tag *tag = (const struct multiboot_tag *)(base + 8);
    while ((const uint8_t *)tag + sizeof(*tag) <= end && tag->size >= 8) {
        if (tag->type == MULTIBOOT_TAG_END) break;
        if ((const uint8_t *)tag + tag->size > end) return 0;
        if (tag->type == MULTIBOOT_TAG_CMDLINE) consume_cmdline(tag);
        if (tag->type == MULTIBOOT_TAG_FRAMEBUFFER) consume_framebuffer(tag);
        if (tag->type == MULTIBOOT_TAG_MMAP) {
            if (tag->size < sizeof(struct multiboot_tag_mmap)) return 0;
            const struct multiboot_tag_mmap *memory_map =
                (const struct multiboot_tag_mmap *)tag;
            if (memory_map->entry_size < sizeof(struct multiboot_mmap_entry)) return 0;
            uint32_t offset = sizeof(*memory_map);
            while (offset + memory_map->entry_size <= tag->size &&
                   descriptor_count < BIOS_MEMORY_DESCRIPTOR_CAPACITY) {
                const struct multiboot_mmap_entry *entry =
                    (const struct multiboot_mmap_entry *)((const uint8_t *)tag + offset);
                if (entry->type == MULTIBOOT_MMAP_AVAILABLE && entry->length >= 4096) {
                    struct bios_memory_descriptor *descriptor =
                        &bios_memory_map[descriptor_count++];
                    descriptor->type = 7;
                    descriptor->padding = 0;
                    descriptor->physical_start = entry->address;
                    descriptor->virtual_start = 0;
                    descriptor->number_of_pages = entry->length / 4096;
                    descriptor->attributes = 0;
                }
                offset += memory_map->entry_size;
            }
        }
        uint32_t next_size = align_up_u32(tag->size, 8);
        if (next_size < tag->size || (const uint8_t *)tag + next_size > end) return 0;
        tag = (const struct multiboot_tag *)((const uint8_t *)tag + next_size);
    }

    if (descriptor_count == 0) return 0;
    bios_boot_info.magic = AKAOS_BOOT_INFO_MAGIC;
    bios_boot_info.version = AKAOS_BOOT_INFO_VERSION;
    bios_boot_info.size = sizeof(bios_boot_info);
    if (!multiboot_uefi_handoff) bios_boot_info.flags |= AKAOS_BOOT_FLAG_BIOS;
    bios_boot_info.memory_map = (uint64_t)(uintptr_t)bios_memory_map;
    bios_boot_info.memory_map_size =
        (uint64_t)descriptor_count * sizeof(struct bios_memory_descriptor);
    bios_boot_info.memory_descriptor_size = sizeof(struct bios_memory_descriptor);
    bios_boot_info.memory_descriptor_version = 1;
    return 1;
}

static int build_multiboot1_boot_info(uint32_t mb_info_addr) {
    if (!mb_info_addr) return 0;
    uint32_t flags = *(const uint32_t *)(uintptr_t)mb_info_addr;

    /* Cmdline at offset 16 */
    if (flags & (1 << 2)) {
        const char *cmdline = (const char *)(uintptr_t)*(const uint32_t *)(uintptr_t)(mb_info_addr + 16);
        if (cmdline) {
            uint32_t len = 0;
            while (cmdline[len]) len++;
            if (text_contains((const uint8_t *)cmdline, len, "AKAOS.TEST") ||
                text_contains((const uint8_t *)cmdline, len, "akaos.test")) {
                bios_boot_info.flags |= AKAOS_BOOT_FLAG_TEST_MODE;
            }
            if (text_contains((const uint8_t *)cmdline, len, "VERBOSE") ||
                text_contains((const uint8_t *)cmdline, len, "verbose")) {
                bios_boot_info.flags |= AKAOS_BOOT_FLAG_VERBOSE;
            }
        }
    }

    /* Framebuffer at offset 88 (bit 11) */
    if (flags & (1 << 11)) {
        uint64_t fb_addr = *(const uint32_t *)(uintptr_t)(mb_info_addr + 88);
        uint32_t fb_pitch = *(const uint32_t *)(uintptr_t)(mb_info_addr + 96);
        uint32_t fb_width = *(const uint32_t *)(uintptr_t)(mb_info_addr + 100);
        uint32_t fb_height = *(const uint32_t *)(uintptr_t)(mb_info_addr + 104);
        uint8_t fb_bpp = *(const uint8_t *)(uintptr_t)(mb_info_addr + 108);
        uint8_t fb_type = *(const uint8_t *)(uintptr_t)(mb_info_addr + 109);

        if (fb_type == 1 && fb_bpp == 32 && fb_width > 0 && fb_height > 0) {
            bios_boot_info.framebuffer.address = fb_addr;
            bios_boot_info.framebuffer.size = (uint64_t)fb_pitch * fb_height;
            bios_boot_info.framebuffer.width = fb_width;
            bios_boot_info.framebuffer.height = fb_height;
            bios_boot_info.framebuffer.pixels_per_scanline = fb_pitch / 4U;
            bios_boot_info.framebuffer.pixel_format = AKAOS_PIXEL_BGRX8888;
            xscore_serial_write("[INFO] Multiboot 1 framebuffer accepted\n");
        }
    }

    /* Memory map at offset 44/48 (bit 6) */
    uint32_t descriptor_count = 0;
    if (flags & (1 << 6)) {
        uint32_t mmap_len = *(const uint32_t *)(uintptr_t)(mb_info_addr + 44);
        uint32_t mmap_addr = *(const uint32_t *)(uintptr_t)(mb_info_addr + 48);
        uint32_t offset = 0;
        while (offset < mmap_len && descriptor_count < BIOS_MEMORY_DESCRIPTOR_CAPACITY) {
            uint32_t size = *(const uint32_t *)(uintptr_t)(mmap_addr + offset);
            uint64_t base_addr = *(const uint64_t *)(uintptr_t)(mmap_addr + offset + 4);
            uint64_t length = *(const uint64_t *)(uintptr_t)(mmap_addr + offset + 12);
            uint32_t type = *(const uint32_t *)(uintptr_t)(mmap_addr + offset + 20);

            if (type == 1 && length >= 4096) {
                struct bios_memory_descriptor *desc = &bios_memory_map[descriptor_count++];
                desc->type = 7;
                desc->padding = 0;
                desc->physical_start = base_addr;
                desc->virtual_start = 0;
                desc->number_of_pages = length / 4096;
                desc->attributes = 0;
            }
            offset += size + 4;
        }
    }

    if (descriptor_count == 0) {
        /* Fallback: 128 MB RAM */
        struct bios_memory_descriptor *desc = &bios_memory_map[0];
        desc->type = 7;
        desc->padding = 0;
        desc->physical_start = 0x100000;
        desc->virtual_start = 0;
        desc->number_of_pages = (127 * 1024 * 1024) / 4096;
        desc->attributes = 0;
        descriptor_count = 1;
    }

    bios_boot_info.magic = AKAOS_BOOT_INFO_MAGIC;
    bios_boot_info.version = AKAOS_BOOT_INFO_VERSION;
    bios_boot_info.size = sizeof(bios_boot_info);
    bios_boot_info.flags |= AKAOS_BOOT_FLAG_BIOS;
    bios_boot_info.memory_map = (uint64_t)(uintptr_t)bios_memory_map;
    bios_boot_info.memory_map_size = (uint64_t)descriptor_count * sizeof(struct bios_memory_descriptor);
    bios_boot_info.memory_descriptor_size = sizeof(struct bios_memory_descriptor);
    bios_boot_info.memory_descriptor_version = 1;
    return 1;
}

__attribute__((noreturn)) void xscore_multiboot_entry_32(uint32_t magic, uint32_t mb_info_addr) {
    xscore_serial_write("[INFO] Multiboot loader handoff received\n");
    int ok = 0;
    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        ok = build_bios_boot_info((uint64_t)mb_info_addr);
    } else {
        ok = build_multiboot1_boot_info(mb_info_addr);
    }
    if (!ok) {
        xscore_serial_write("[FATAL] Invalid Multiboot information\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    xscore_entry(&bios_boot_info);
    for (;;) __asm__ volatile("cli; hlt");
}

__attribute__((noreturn)) void xscore_bios_entry(uint64_t multiboot_info_address) {
    xscore_serial_write("[INFO] BIOS Multiboot2 loader handoff received\n");
    if (!build_bios_boot_info(multiboot_info_address)) {
        xscore_serial_write("[FATAL] Invalid BIOS Multiboot2 information\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    xscore_entry(&bios_boot_info);
    for (;;) __asm__ volatile("cli; hlt");
}
