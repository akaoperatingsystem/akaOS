#include "uefi.h"
#include <akaos/boot_info.h>

#define MEMORY_MAP_CAPACITY (128U * 1024U)
#define PT_LOAD 1U

static EFI_SYSTEM_TABLE *g_system_table;
static struct akaos_boot_info g_boot_info;
static uint8_t g_memory_map[MEMORY_MAP_CAPACITY] __attribute__((aligned(16)));

static const EFI_GUID loaded_image_guid = {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const EFI_GUID simple_fs_guid = {0x0964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const EFI_GUID gop_guid = {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
static const EFI_GUID acpi_20_guid = {0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
static const EFI_GUID acpi_10_guid = {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};
static const EFI_GUID smbios_guid = {0xeb9d2d31, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};
static const EFI_GUID smbios3_guid = {0xf2fd1544, 0x9794, 0x4a2c, {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94}};

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b) {
    if (a->data1 != b->data1 || a->data2 != b->data2 || a->data3 != b->data3) return 0;
    for (int i = 0; i < 8; ++i) {
        if (a->data4[i] != b->data4[i]) return 0;
    }
    return 1;
}

static void detect_acpi_table(void) {
    if (!g_system_table || !g_system_table->configuration_table) return;
    EFI_CONFIGURATION_TABLE *tables = (EFI_CONFIGURATION_TABLE *)g_system_table->configuration_table;
    for (UINTN i = 0; i < g_system_table->number_of_table_entries; ++i) {
        if (guid_equal(&tables[i].vendor_guid, &acpi_20_guid)) {
            g_boot_info.acpi_rsdp = (uint64_t)(uintptr_t)tables[i].vendor_table;
            break;
        }
    }
    if (!g_boot_info.acpi_rsdp) {
        for (UINTN i = 0; i < g_system_table->number_of_table_entries; ++i) {
            if (guid_equal(&tables[i].vendor_guid, &acpi_10_guid)) {
                g_boot_info.acpi_rsdp = (uint64_t)(uintptr_t)tables[i].vendor_table;
                break;
            }
        }
    }
    for (UINTN i = 0; i < g_system_table->number_of_table_entries; ++i) {
        if (guid_equal(&tables[i].vendor_guid, &smbios3_guid) ||
            guid_equal(&tables[i].vendor_guid, &smbios_guid)) {
            g_boot_info.smbios_table = (uint64_t)(uintptr_t)tables[i].vendor_table;
            break;
        }
    }
}

struct elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
};

struct elf64_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

struct efi_file_info {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
    uint8_t create_time[16];
    uint8_t last_access_time[16];
    uint8_t modification_time[16];
    uint64_t attribute;
    CHAR16 file_name[1];
};

static const EFI_GUID efi_file_info_guid = {
    UINT32_C(0x09576e92), UINT16_C(0x6d3f), UINT16_C(0x11d2),
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    out8(0x3f8 + 1, 0x00);
    out8(0x3f8 + 3, 0x80);
    out8(0x3f8 + 0, 0x03);
    out8(0x3f8 + 1, 0x00);
    out8(0x3f8 + 3, 0x03);
    out8(0x3f8 + 2, 0xc7);
    out8(0x3f8 + 4, 0x0b);
}

static void serial_write(const char *text) {
    while (*text) {
        if (*text == '\n') {
            while ((in8(0x3f8 + 5) & 0x20) == 0) {}
            out8(0x3f8, '\r');
        }
        while ((in8(0x3f8 + 5) & 0x20) == 0) {}
        out8(0x3f8, (uint8_t)*text++);
    }
}

static void serial_write_hex64(const char *label, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    char encoded[17];
    for (unsigned int index = 0; index < 16U; ++index) {
        unsigned int shift = (15U - index) * 4U;
        encoded[index] = digits[(value >> shift) & 0xfU];
    }
    encoded[16] = '\0';
    serial_write(label);
    serial_write(encoded);
    serial_write("\n");
}

static void console_write(CHAR16 *text) {
    g_system_table->con_out->output_string(g_system_table->con_out, text);
}

static void memory_zero(void *destination, UINTN count) {
    uint8_t *dst = destination;
    while (count--) *dst++ = 0;
}

static EFI_STATUS open_root(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL **root) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *filesystem = 0;
    EFI_STATUS status = g_system_table->boot_services->handle_protocol(
        image_handle, (EFI_GUID *)&loaded_image_guid, (void **)&loaded_image);
    if (EFI_ERROR(status)) return status;
    status = g_system_table->boot_services->handle_protocol(
        loaded_image->device_handle, (EFI_GUID *)&simple_fs_guid, (void **)&filesystem);
    if (EFI_ERROR(status)) return status;
    return filesystem->open_volume(filesystem, root);
}

static EFI_STATUS read_kernel(EFI_FILE_PROTOCOL *root, EFI_FILE_PROTOCOL **file_out,
                              UINTN *size) {
    EFI_FILE_PROTOCOL *file = 0;
    EFI_STATUS status = root->open(root, &file, L"\\XSCORE.ELF", EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status)) return status;

    UINTN info_size = 0;
    status = file->get_info(file, &efi_file_info_guid, &info_size, 0);
    if (status != EFI_BUFFER_TOO_SMALL ||
        info_size < offsetof(struct efi_file_info, file_name)) {
        file->close(file);
        return EFI_ERROR(status) ? status : UINT64_C(0x8000000000000001);
    }

    void *info_buffer = 0;
    status = g_system_table->boot_services->allocate_pool(
        EFI_LOADER_DATA, info_size, &info_buffer);
    if (EFI_ERROR(status)) {
        file->close(file);
        return status;
    }
    status = file->get_info(file, &efi_file_info_guid, &info_size, info_buffer);
    if (EFI_ERROR(status) ||
        info_size < offsetof(struct efi_file_info, file_name)) {
        g_system_table->boot_services->free_pool(info_buffer);
        file->close(file);
        return EFI_ERROR(status) ? status : UINT64_C(0x8000000000000001);
    }

    uint64_t kernel_size = ((struct efi_file_info *)info_buffer)->file_size;
    g_system_table->boot_services->free_pool(info_buffer);
    if (kernel_size < sizeof(struct elf64_header) ||
        kernel_size > (uint64_t)(UINTN)-1) {
        file->close(file);
        return UINT64_C(0x8000000000000001);
    }
    *file_out = file;
    *size = (UINTN)kernel_size;
    return EFI_SUCCESS;
}

static EFI_STATUS read_file_region(EFI_FILE_PROTOCOL *file, uint64_t offset,
                                   void *destination, UINTN length) {
    EFI_STATUS status = file->set_position(file, offset);
    if (EFI_ERROR(status)) return status;
    UINTN bytes_read = length;
    status = file->read(file, &bytes_read, destination);
    if (EFI_ERROR(status)) return status;
    return bytes_read == length ? EFI_SUCCESS : UINT64_C(0x8000000000000001);
}

static int validate_elf(const struct elf64_header *header, UINTN file_size) {
    if (file_size < sizeof(*header)) return 0;
    if (header->ident[0] != 0x7f || header->ident[1] != 'E' || header->ident[2] != 'L' || header->ident[3] != 'F') return 0;
    if (header->ident[4] != 2 || header->ident[5] != 1 || header->machine != 0x3e) return 0;
    if (header->phentsize != sizeof(struct elf64_program_header)) return 0;
    uint64_t table_size = (uint64_t)header->phnum * header->phentsize;
    if (header->phoff > file_size || table_size > file_size - header->phoff) return 0;
    return 1;
}

static EFI_STATUS load_elf(EFI_FILE_PROTOCOL *file, UINTN file_size,
                           uint64_t *entry) {
    struct elf64_header header;
    EFI_STATUS status = read_file_region(file, 0, &header, sizeof(header));
    if (EFI_ERROR(status) || !validate_elf(&header, file_size)) {
        serial_write("[FATAL] XSCORE ELF header validation failed\n");
        return EFI_ERROR(status) ? status : UINT64_C(0x8000000000000001);
    }

    uint64_t table_size = (uint64_t)header.phnum * header.phentsize;
    if (table_size == 0 || table_size > (uint64_t)(UINTN)-1) {
        return UINT64_C(0x8000000000000001);
    }
    struct elf64_program_header *segments = 0;
    status = g_system_table->boot_services->allocate_pool(
        EFI_LOADER_DATA, (UINTN)table_size, (void **)&segments);
    if (EFI_ERROR(status)) return status;
    status = read_file_region(file, header.phoff, segments, (UINTN)table_size);
    if (EFI_ERROR(status)) {
        g_system_table->boot_services->free_pool(segments);
        return status;
    }

    EFI_STATUS result = EFI_SUCCESS;
    for (uint16_t index = 0; index < header.phnum; ++index) {
        struct elf64_program_header *segment = &segments[index];
        if (segment->type != PT_LOAD || segment->memsz == 0) continue;
        if (segment->filesz > segment->memsz || segment->offset > file_size ||
            segment->filesz > file_size - segment->offset) {
            serial_write("[FATAL] XSCORE ELF segment bounds invalid\n");
            result = UINT64_C(0x8000000000000001);
            break;
        }

        EFI_PHYSICAL_ADDRESS allocation = segment->paddr & ~UINT64_C(0xfff);
        uint64_t page_offset = segment->paddr - allocation;
        if (segment->memsz > UINT64_MAX - page_offset - UINT64_C(0xfff)) {
            result = UINT64_C(0x8000000000000001);
            break;
        }
        UINTN pages = (UINTN)((page_offset + segment->memsz + 0xfff) / 0x1000);
        serial_write("[INFO] allocating XSCORE ELF segment\n");
        serial_write_hex64("[INFO] XSCORE segment address 0x", segment->paddr);
        serial_write_hex64("[INFO] XSCORE segment size 0x", segment->memsz);
        /* UEFI firmware may have fragmented low memory. Allocate the segment
         * one page at a time so a large initramfs-backed data segment does
         * not require one enormous contiguous range. */
        EFI_STATUS status = EFI_SUCCESS;
        for (UINTN page = 0; page < pages; ++page) {
            EFI_PHYSICAL_ADDRESS page_address = allocation + (page * 0x1000);
            status = g_system_table->boot_services->allocate_pages(
                EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, 1, &page_address);
            if (EFI_ERROR(status)) {
                serial_write_hex64("[FATAL] XSCORE allocation failed at 0x", page_address);
                serial_write_hex64("[FATAL] UEFI allocation status 0x", status);
                break;
            }
        }
        if (EFI_ERROR(status)) {
            serial_write("[FATAL] XSCORE ELF segment allocation failed\n");
            result = status;
            break;
        }

        void *destination = (void *)(uintptr_t)segment->paddr;
        memory_zero(destination, (UINTN)segment->memsz);
        if (segment->filesz != 0) {
            serial_write("[INFO] reading XSCORE ELF segment from disk\n");
            status = read_file_region(file, segment->offset, destination,
                                      (UINTN)segment->filesz);
            if (EFI_ERROR(status)) {
                serial_write("[FATAL] Unable to read XSCORE ELF segment\n");
                result = status;
                break;
            }
        }
    }
    if (!EFI_ERROR(result)) *entry = header.entry;
    g_system_table->boot_services->free_pool(segments);
    return result;
}

static EFI_STATUS capture_framebuffer(void) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status = g_system_table->boot_services->locate_protocol(
        (EFI_GUID *)&gop_guid, 0, (void **)&gop);
    if (EFI_ERROR(status) || !gop || !gop->mode || !gop->mode->info) return status;
    g_boot_info.framebuffer.address = gop->mode->frame_buffer_base;
    g_boot_info.framebuffer.size = gop->mode->frame_buffer_size;
    g_boot_info.framebuffer.width = gop->mode->info->horizontal_resolution;
    g_boot_info.framebuffer.height = gop->mode->info->vertical_resolution;
    g_boot_info.framebuffer.pixels_per_scanline = gop->mode->info->pixels_per_scanline;
    g_boot_info.framebuffer.pixel_format = gop->mode->info->pixel_format;
    return EFI_SUCCESS;
}

static void detect_test_mode(EFI_FILE_PROTOCOL *root) {
    EFI_FILE_PROTOCOL *marker = 0;
    EFI_STATUS status = root->open(root, &marker, L"\\AKAOS.TEST", EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(status)) {
        g_boot_info.flags |= AKAOS_BOOT_FLAG_TEST_MODE;
        marker->close(marker);
    }
}

static int wcs_contains_ascii_ci(const CHAR16 *wstr, UINTN length, const char *sub) {
    if (!wstr || !sub || !*sub) return 0;
    UINTN sub_len = 0;
    while (sub[sub_len]) ++sub_len;
    if (length < sub_len) return 0;
    for (UINTN i = 0; i <= length - sub_len; ++i) {
        UINTN j = 0;
        for (; j < sub_len; ++j) {
            CHAR16 c = wstr[i + j];
            char s = sub[j];
            if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
            if (s >= 'A' && s <= 'Z') s += ('a' - 'A');
            if ((char)c != s) break;
        }
        if (j == sub_len) return 1;
    }
    return 0;
}

static void detect_boot_mode(EFI_FILE_PROTOCOL *root, EFI_HANDLE image_handle) {
    EFI_FILE_PROTOCOL *marker = 0;
    // 1. Check filesystem markers on the EFI partition
    if (!EFI_ERROR(root->open(root, &marker, L"\\VERBOSE", EFI_FILE_MODE_READ, 0)) ||
        !EFI_ERROR(root->open(root, &marker, L"\\AKAOS.VERBOSE", EFI_FILE_MODE_READ, 0)) ||
        !EFI_ERROR(root->open(root, &marker, L"\\TEXT", EFI_FILE_MODE_READ, 0)) ||
        !EFI_ERROR(root->open(root, &marker, L"\\CONSOLE", EFI_FILE_MODE_READ, 0))) {
        g_boot_info.flags |= AKAOS_BOOT_FLAG_VERBOSE;
        marker->close(marker);
        serial_write("[INFO] Verbose text console boot flag detected via marker\n");
    }
    if (!EFI_ERROR(root->open(root, &marker, L"\\SPLASH", EFI_FILE_MODE_READ, 0)) ||
        !EFI_ERROR(root->open(root, &marker, L"\\QUIET", EFI_FILE_MODE_READ, 0))) {
        g_boot_info.flags &= ~AKAOS_BOOT_FLAG_VERBOSE;
        g_boot_info.flags |= AKAOS_BOOT_FLAG_SPLASH;
        marker->close(marker);
        serial_write("[INFO] Graphical splash boot flag detected via marker\n");
    }

    // 2. Check EFI LoadedImage protocol command-line options
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = 0;
    EFI_STATUS status = g_system_table->boot_services->handle_protocol(
        image_handle, (EFI_GUID *)&loaded_image_guid, (void **)&loaded_image);
    if (!EFI_ERROR(status) && loaded_image && loaded_image->load_options && loaded_image->load_options_size > 0) {
        const CHAR16 *opts = (const CHAR16 *)loaded_image->load_options;
        UINTN opt_chars = loaded_image->load_options_size / sizeof(CHAR16);
        if (wcs_contains_ascii_ci(opts, opt_chars, "verbose") ||
            wcs_contains_ascii_ci(opts, opt_chars, "text") ||
            wcs_contains_ascii_ci(opts, opt_chars, "console")) {
            g_boot_info.flags |= AKAOS_BOOT_FLAG_VERBOSE;
            g_boot_info.flags &= ~AKAOS_BOOT_FLAG_SPLASH;
            serial_write("[INFO] Verbose text console boot requested via LoadOptions\n");
        } else if (wcs_contains_ascii_ci(opts, opt_chars, "splash") ||
                   wcs_contains_ascii_ci(opts, opt_chars, "quiet")) {
            g_boot_info.flags &= ~AKAOS_BOOT_FLAG_VERBOSE;
            g_boot_info.flags |= AKAOS_BOOT_FLAG_SPLASH;
            serial_write("[INFO] Graphical splash boot requested via LoadOptions\n");
        }
    }
}

static EFI_STATUS exit_boot_services(EFI_HANDLE image_handle) {
    EFI_BOOT_SERVICES *boot_services = g_system_table->boot_services;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        UINTN map_size = MEMORY_MAP_CAPACITY;
        UINTN map_key = 0;
        UINTN descriptor_size = 0;
        UINT32 descriptor_version = 0;
        EFI_STATUS status = boot_services->get_memory_map(
            &map_size, g_memory_map, &map_key, &descriptor_size, &descriptor_version);
        if (EFI_ERROR(status)) return status;
        g_boot_info.memory_map = (uint64_t)(uintptr_t)g_memory_map;
        g_boot_info.memory_map_size = map_size;
        g_boot_info.memory_descriptor_size = descriptor_size;
        g_boot_info.memory_descriptor_version = descriptor_version;
        status = boot_services->exit_boot_services(image_handle, map_key);
        if (!EFI_ERROR(status)) return EFI_SUCCESS;
    }
    return UINT64_C(0x8000000000000002);
}

typedef void(__attribute__((sysv_abi)) *kernel_entry_t)(const struct akaos_boot_info *);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    g_system_table = system_table;
    g_boot_info.uefi_system_table = (uint64_t)(uintptr_t)system_table;
    g_boot_info.uefi_runtime_services = (uint64_t)(uintptr_t)system_table->runtime_services;
    serial_init();
    serial_write("[INFO] akaOS UEFI loader starting\n");
    console_write(L"akaOS UEFI loader\r\n");

    EFI_FILE_PROTOCOL *root = 0;
    EFI_STATUS status = open_root(image_handle, &root);
    if (EFI_ERROR(status)) {
        serial_write("[FATAL] Unable to open the EFI system partition\n");
        return status;
    }
    detect_test_mode(root);
    detect_boot_mode(root, image_handle);
    detect_acpi_table();

    /* Capture graphics metadata only. The loader never renders the splash;
     * Stage 1 belongs to the early console and Stage 2 belongs to XSCore. */
    status = capture_framebuffer();
    if (EFI_ERROR(status)) {
        serial_write("[FATAL] UEFI framebuffer unavailable\n");
        return status;
    }
    serial_write("[INFO] UEFI framebuffer initialized\n");

    EFI_FILE_PROTOCOL *kernel_file = 0;
    UINTN kernel_size = 0;
    status = read_kernel(root, &kernel_file, &kernel_size);
    root->close(root);
    if (EFI_ERROR(status)) {
        serial_write("[FATAL] Unable to read XSCORE.ELF\n");
        return status;
    }

    uint64_t kernel_entry = 0;
    status = load_elf(kernel_file, kernel_size, &kernel_entry);
    EFI_STATUS close_status = kernel_file->close(kernel_file);
    if (EFI_ERROR(close_status)) return close_status;
    if (EFI_ERROR(status)) {
        serial_write("[FATAL] Invalid or unloadable XSCore ELF image\n");
        return status;
    }
    g_boot_info.magic = AKAOS_BOOT_INFO_MAGIC;
    g_boot_info.version = AKAOS_BOOT_INFO_VERSION;
    g_boot_info.size = sizeof(g_boot_info);
    serial_write("[INFO] XSCore ELF loaded\n");
    serial_write("[INFO] Exiting UEFI boot services\n");

    status = exit_boot_services(image_handle);
    if (EFI_ERROR(status)) return status;

    ((kernel_entry_t)(uintptr_t)kernel_entry)(&g_boot_info);
    for (;;) __asm__ volatile("cli; hlt");
}
