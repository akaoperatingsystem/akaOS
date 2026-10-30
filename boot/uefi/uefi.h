#ifndef AKAOS_UEFI_H
#define AKAOS_UEFI_H

#include <stddef.h>
#include <stdint.h>

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t UINTN;
typedef uint32_t UINT32;
typedef uint16_t CHAR16;
typedef uint8_t BOOLEAN;

#define EFIAPI __attribute__((ms_abi))
#define EFI_SUCCESS 0
#define EFI_BUFFER_TOO_SMALL (UINT64_C(0x8000000000000005))
#define EFI_ERROR(status) (((int64_t)(status)) < 0)
#define EFI_LOADER_DATA 2
#define EFI_ALLOCATE_MAX_ADDRESS 1
#define EFI_ALLOCATE_ADDRESS 2
#define EFI_FILE_MODE_READ UINT64_C(0x0000000000000001)

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} EFI_GUID;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS(EFIAPI *reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, BOOLEAN);
    EFI_STATUS(EFIAPI *output_string)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
    void *test_string;
    void *query_mode;
    void *set_mode;
    void *set_attribute;
    void *clear_screen;
    void *set_cursor_position;
    void *enable_cursor;
    void *mode;
};

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
    uint64_t revision;
    EFI_STATUS(EFIAPI *open)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, CHAR16 *, uint64_t, uint64_t);
    EFI_STATUS(EFIAPI *close)(EFI_FILE_PROTOCOL *);
    void *delete_file;
    EFI_STATUS(EFIAPI *read)(EFI_FILE_PROTOCOL *, UINTN *, void *);
    void *write;
    void *get_position;
    EFI_STATUS(EFIAPI *set_position)(EFI_FILE_PROTOCOL *, uint64_t);
    EFI_STATUS(EFIAPI *get_info)(EFI_FILE_PROTOCOL *, const EFI_GUID *, UINTN *, void *);
    void *set_info;
    void *flush;
};

typedef struct {
    uint64_t revision;
    EFI_STATUS(EFIAPI *open_volume)(void *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    uint32_t version;
    EFI_HANDLE parent_handle;
    void *system_table;
    EFI_HANDLE device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    void *load_options;
    void *image_base;
    uint64_t image_size;
    uint32_t image_code_type;
    uint32_t image_data_type;
    void *unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    struct {
        uint32_t red_mask;
        uint32_t green_mask;
        uint32_t blue_mask;
        uint32_t reserved_mask;
    } pixel_information;
    uint32_t pixels_per_scanline;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t max_mode;
    uint32_t mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    UINTN size_of_info;
    EFI_PHYSICAL_ADDRESS frame_buffer_base;
    UINTN frame_buffer_size;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void *query_mode;
    void *set_mode;
    void *blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
};

typedef struct {
    EFI_TABLE_HEADER hdr;
    void *raise_tpl;
    void *restore_tpl;
    EFI_STATUS(EFIAPI *allocate_pages)(uint32_t, uint32_t, UINTN, EFI_PHYSICAL_ADDRESS *);
    EFI_STATUS(EFIAPI *free_pages)(EFI_PHYSICAL_ADDRESS, UINTN);
    EFI_STATUS(EFIAPI *get_memory_map)(UINTN *, void *, UINTN *, UINTN *, UINT32 *);
    EFI_STATUS(EFIAPI *allocate_pool)(uint32_t, UINTN, void **);
    EFI_STATUS(EFIAPI *free_pool)(void *);
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    EFI_STATUS(EFIAPI *handle_protocol)(EFI_HANDLE, EFI_GUID *, void **);
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    EFI_STATUS(EFIAPI *exit_boot_services)(EFI_HANDLE, UINTN);
    void *get_next_monotonic_count;
    void *stall;
    void *set_watchdog_timer;
    void *connect_controller;
    void *disconnect_controller;
    void *open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    void *locate_handle_buffer;
    EFI_STATUS(EFIAPI *locate_protocol)(EFI_GUID *, void *, void **);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_GUID vendor_guid;
    void *vendor_table;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    EFI_TABLE_HEADER hdr;
    CHAR16 *firmware_vendor;
    uint32_t firmware_revision;
    uint32_t padding;
    EFI_HANDLE console_in_handle;
    void *con_in;
    EFI_HANDLE console_out_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *con_out;
    EFI_HANDLE standard_error_handle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *std_err;
    void *runtime_services;
    EFI_BOOT_SERVICES *boot_services;
    UINTN number_of_table_entries;
    void *configuration_table;
} EFI_SYSTEM_TABLE;

#endif

