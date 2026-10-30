#include <xscore/initramfs.h>
#include <xscore/serial.h>
#include <xscore/vfs.h>
#include <stddef.h>
#include <stdint.h>

#define INITRAMFS_MAGIC "AKAOSRAM"
#define INITRAMFS_VERSION 1U

#define INITRAMFS_TYPE_DIR 1U
#define INITRAMFS_TYPE_FILE 2U

struct __attribute__((packed)) initramfs_header {
    char magic[8];
    uint32_t version;
    uint32_t entry_count;
    uint64_t total_size;
};

struct __attribute__((packed)) initramfs_entry {
    char path[128];
    uint32_t type;
    uint32_t mode;
    uint64_t size;
    uint64_t data_offset;
};

#define akaos_initramfs_header initramfs_header
#define akaos_initramfs_entry initramfs_entry

int xscore_initramfs_mount_archive(const uint8_t *archive, size_t archive_size) {
    if (!archive || archive_size < sizeof(struct initramfs_header)) {
        xscore_serial_write("[FATAL] Initramfs archive too small\n");
        return 0;
    }

    const struct initramfs_header *header = (const void *)archive;
    for (size_t i = 0; i < 8; ++i) {
        if (header->magic[i] != INITRAMFS_MAGIC[i]) {
            xscore_serial_write("[FATAL] Invalid initramfs magic signature\n");
            return 0;
        }
    }

    if (header->version != INITRAMFS_VERSION) {
        xscore_serial_write("[FATAL] Unsupported initramfs version\n");
        return 0;
    }

    size_t header_and_table_size =
        sizeof(struct initramfs_header) +
        (size_t)header->entry_count * sizeof(struct initramfs_entry);
    if (archive_size < header_and_table_size) {
        xscore_serial_write("[FATAL] Initramfs header+table truncated\n");
        return 0;
    }

    const struct initramfs_entry *entries =
        (const void *)(archive + sizeof(struct initramfs_header));
    for (uint32_t i = 0; i < header->entry_count; ++i) {
        const struct initramfs_entry *entry = &entries[i];

        /* Ensure null-terminated path */
        char path_buf[129];
        for (size_t c = 0; c < 128; ++c) {
            path_buf[c] = entry->path[c];
        }
        path_buf[128] = '\0';

        if (entry->type == INITRAMFS_TYPE_DIR) {
            if (!xscore_vfs_add_directory(path_buf, entry->mode)) {
                xscore_serial_write("[FATAL] Failed to add initramfs directory: ");
                xscore_serial_write(path_buf);
                xscore_serial_write("\n");
                return 0;
            }
        } else if (entry->type == INITRAMFS_TYPE_FILE) {
            if (entry->data_offset + entry->size > archive_size) {
                xscore_serial_write("[FATAL] Initramfs file payload overflows archive\n");
                return 0;
            }
            const uint8_t *data = archive + entry->data_offset;
            if (!xscore_vfs_add_file(path_buf, data, (size_t)entry->size, entry->mode)) {
                xscore_serial_write("[FATAL] Failed to add initramfs file: ");
                xscore_serial_write(path_buf);
                xscore_serial_write("\n");
                return 0;
            }
        } else {
            xscore_serial_write("[FATAL] Unknown initramfs entry type: ");
            xscore_serial_write(path_buf);
            xscore_serial_write("\n");
            return 0;
        }
    }
    if (!xscore_vfs_install_devfs()) {
        xscore_serial_write("[FATAL] Failed to install /dev namespace\n");
        return 0;
    }
    if (!xscore_vfs_install_procfs()) {
        xscore_serial_write("[FATAL] Failed to install /proc namespace\n");
        return 0;
    }
    return 1;
}

#if defined(__i386__)
extern const uint8_t _binary_initramfs32_img_start[];
extern const uint8_t _binary_initramfs32_img_end[];

int xscore_initramfs_initialize(void) {
    if (!xscore_vfs_initialize()) return 0;
    const uint8_t *archive = _binary_initramfs32_img_start;
    size_t archive_size = (size_t)(_binary_initramfs32_img_end - archive);
    return xscore_initramfs_mount_archive(archive, archive_size);
}
#elif defined(__aarch64__)
extern const uint8_t _binary_initramfs_aarch64_img_start[];
extern const uint8_t _binary_initramfs_aarch64_img_end[];

int xscore_initramfs_initialize(void) {
    if (!xscore_vfs_initialize()) return 0;
    const uint8_t *archive = _binary_initramfs_aarch64_img_start;
    size_t archive_size = (size_t)(_binary_initramfs_aarch64_img_end - archive);
    return xscore_initramfs_mount_archive(archive, archive_size);
}
#elif defined(__arm__)
extern const uint8_t _binary_initramfs_arm32_img_start[];
extern const uint8_t _binary_initramfs_arm32_img_end[];

int xscore_initramfs_initialize(void) {
    if (!xscore_vfs_initialize()) return 0;
    const uint8_t *archive = _binary_initramfs_arm32_img_start;
    size_t archive_size = (size_t)(_binary_initramfs_arm32_img_end - archive);
    return xscore_initramfs_mount_archive(archive, archive_size);
}
#elif defined(__riscv)
#if __riscv_xlen == 64
extern const uint8_t _binary_initramfs_riscv64_img_start[];
extern const uint8_t _binary_initramfs_riscv64_img_end[];

int xscore_initramfs_initialize(void) {
    if (!xscore_vfs_initialize()) return 0;
    const uint8_t *archive = _binary_initramfs_riscv64_img_start;
    size_t archive_size = (size_t)(_binary_initramfs_riscv64_img_end - archive);
    return xscore_initramfs_mount_archive(archive, archive_size);
}
#else
extern const uint8_t _binary_initramfs_riscv32_img_start[];
extern const uint8_t _binary_initramfs_riscv32_img_end[];

int xscore_initramfs_initialize(void) {
    if (!xscore_vfs_initialize()) return 0;
    const uint8_t *archive = _binary_initramfs_riscv32_img_start;
    size_t archive_size = (size_t)(_binary_initramfs_riscv32_img_end - archive);
    return xscore_initramfs_mount_archive(archive, archive_size);
}
#endif
#else
extern const uint8_t _binary_initramfs_img_start[];
extern const uint8_t _binary_initramfs_img_end[];

int xscore_initramfs_initialize(void) {
    if (!xscore_vfs_initialize()) return 0;
    const uint8_t *archive = _binary_initramfs_img_start;
    size_t archive_size = (size_t)(_binary_initramfs_img_end - archive);
    return xscore_initramfs_mount_archive(archive, archive_size);
}
#endif
