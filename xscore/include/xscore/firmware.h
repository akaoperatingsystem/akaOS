#ifndef XSCORE_FIRMWARE_H
#define XSCORE_FIRMWARE_H

#include <stdint.h>
#include <stddef.h>
#include <akaos/boot_info.h>

/* ================================================================
 * UEFI Specification Types & GUIDs
 * ================================================================ */

typedef uint64_t efi_status_t;

struct efi_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
};

struct efi_time {
    uint16_t year;       /* 1900 - 9999 */
    uint8_t  month;      /* 1 - 12 */
    uint8_t  day;        /* 1 - 31 */
    uint8_t  hour;       /* 0 - 23 */
    uint8_t  minute;     /* 0 - 59 */
    uint8_t  second;     /* 0 - 59 */
    uint8_t  pad1;
    uint32_t nanosecond; /* 0 - 999,999,999 */
    int16_t  time_zone;  /* -1440 to 1440 or 2047 (unspecified) */
    uint8_t  daylight;
    uint8_t  pad2;
};

struct efi_time_capabilities {
    uint32_t resolution;
    uint32_t accuracy;
    uint8_t  sets_to_zero;
};

/* UEFI Variable Attributes */
#define EFI_VARIABLE_NON_VOLATILE                           0x00000001U
#define EFI_VARIABLE_BOOTSERVICE_ACCESS                     0x00000002U
#define EFI_VARIABLE_RUNTIME_ACCESS                         0x00000004U
#define EFI_VARIABLE_HARDWARE_ERROR_RECORD                  0x00000008U
#define EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS             0x00000010U
#define EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS  0x00000020U
#define EFI_VARIABLE_APPEND_WRITE                           0x00000040U

/* UEFI Reset Types */
#define EFI_RESET_COLD     0
#define EFI_RESET_WARM     1
#define EFI_RESET_SHUTDOWN 2
#define EFI_RESET_PLATFORM 3

/* Standard Global Variable GUID: 8BE4DF61-93CA-11D2-AA0D-00E098032B8C */
#define EFI_GLOBAL_VARIABLE_GUID \
    { 0x8BE4DF61, 0x93CA, 0x11D2, { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } }

/* Security Database GUID (db, dbx, dbr, dbt): D719B2CB-3D3A-4596-A3BC-DAD00E67656F */
#define EFI_IMAGE_SECURITY_DATABASE_GUID \
    { 0xD719B2CB, 0x3D3A, 0x4596, { 0xA3, 0xBC, 0xDA, 0xD0, 0x0E, 0x67, 0x65, 0x6F } }

/* ================================================================
 * UEFI Runtime Services Table Layout
 * ================================================================ */

struct efi_table_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
};

struct efi_runtime_services {
    struct efi_table_header hdr;
    void *get_time;
    void *set_time;
    void *get_wakeup_time;
    void *set_wakeup_time;
    void *set_virtual_address_map;
    void *convert_pointer;
    void *get_variable;
    void *get_next_variable_name;
    void *set_variable;
    void *get_next_high_monotonic_count;
    void *reset_system;
    void *update_capsule;
    void *query_capsule_capabilities;
    void *query_variable_info;
};

/* Capsule Update Structures */
struct efi_capsule_header {
    struct efi_guid capsule_guid;
    uint32_t header_size;
    uint32_t flags;
    uint32_t capsule_image_size;
};

struct efi_capsule_block_descriptor {
    uint64_t length;
    union {
        uint64_t data_block;
        uint64_t continuation_pointer;
    } address;
};

#define CAPSULE_FLAGS_PERSIST_ACROSS_RESET 0x00010000U
#define CAPSULE_FLAGS_POPULATE_SYSTEM_TABLE 0x00020000U
#define CAPSULE_FLAGS_INITIATE_RESET       0x00040000U

/* ================================================================
 * Legacy BIOS Structures & Real-Mode Thunking
 * ================================================================ */

struct bios_regs {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t eflags;
};

/* PC-Compatible BIOS Data Area (0x400 - 0x4FF) */
struct bios_data_area {
    uint16_t com_base[4];      /* 0x400: COM1-COM4 I/O port bases */
    uint16_t lpt_base[3];      /* 0x408: LPT1-LPT3 I/O port bases */
    uint16_t ebda_base_seg;    /* 0x40E: EBDA base segment address */
    uint16_t hardware_flags;   /* 0x410: Installed hardware flags */
    uint8_t  pad1;
    uint16_t base_memory_kb;   /* 0x413: Base conventional memory in KiB (typically 640) */
    uint16_t pad2;
    uint8_t  keyboard_flags[2];/* 0x417: Keyboard shift & toggle states */
    uint8_t  pad3[5];
    uint16_t kbd_buf_head;     /* 0x41A: Keyboard buffer head pointer */
    uint16_t kbd_buf_tail;     /* 0x41C: Keyboard buffer tail pointer */
    uint8_t  kbd_buffer[32];   /* 0x41E: Keyboard circular buffer */
    uint8_t  drive_calib_status;/* 0x43E: Floppy drive calibration */
    uint8_t  drive_motor_status;/* 0x43F: Floppy disk motor status */
    uint8_t  motor_timeout;    /* 0x440: Floppy motor shutoff counter */
    uint8_t  disk_status;      /* 0x441: Last diskette operation status */
    uint8_t  fdc_status[7];    /* 0x442: FDC controller status registers */
    uint8_t  video_mode;       /* 0x449: Current active video mode */
    uint16_t video_columns;    /* 0x44A: Screen character columns */
    uint16_t video_page_bytes; /* 0x44C: Video regen buffer page size in bytes */
    uint16_t video_page_offset;/* 0x44E: Current page start offset */
    uint16_t cursor_pos[8];    /* 0x450: Cursor row/column for pages 0-7 */
    uint16_t cursor_type;      /* 0x460: Cursor start/end scan lines */
    uint8_t  active_video_page;/* 0x462: Active display page number */
    uint16_t crtc_io_port;     /* 0x463: CRT controller base I/O port */
    uint8_t  crtc_mode_reg;    /* 0x465: Current 6845 mode select register */
    uint8_t  crtc_color_reg;   /* 0x466: CGA color palette register */
    uint32_t timer_ticks;      /* 0x46C: Daily timer ticks since midnight */
    uint8_t  timer_overflow;   /* 0x470: Timer rolled past 24 hours */
    uint8_t  break_flag;       /* 0x471: Ctrl-Break key hit */
    uint16_t soft_reset_flag;  /* 0x472: POST warm boot marker (0x1234) */
} __attribute__((packed));

/* ================================================================
 * NVRAM Variable Store Representation
 * ================================================================ */

#define EFIVAR_MAX_NAME_LEN 64
#define EFIVAR_MAX_DATA_LEN 512
#define EFIVAR_MAX_VARS     32

struct efi_variable {
    char name[EFIVAR_MAX_NAME_LEN];
    struct efi_guid guid;
    uint32_t attributes;
    uint64_t data_size;
    uint8_t data[EFIVAR_MAX_DATA_LEN];
    uint8_t authenticated;
};

/* ================================================================
 * Core Subsystem Interfaces
 * ================================================================ */

void xscore_firmware_initialize(const struct akaos_boot_info *boot_info);
int xscore_firmware_is_uefi(void);

/* UEFI Runtime Service Wrappers */
int xscore_uefi_get_time(struct efi_time *time);
int xscore_uefi_set_time(const struct efi_time *time);
int xscore_uefi_get_variable(const char *name, const struct efi_guid *guid,
                             uint32_t *attributes, uint64_t *data_size, void *data);
int xscore_uefi_set_variable(const char *name, const struct efi_guid *guid,
                             uint32_t attributes, uint64_t data_size, const void *data);
int xscore_uefi_get_next_variable_name(uint64_t *name_size, char *name,
                                      struct efi_guid *guid);
int xscore_uefi_reset_system(uint32_t reset_type, uint64_t status,
                             uint64_t data_size, const void *data);
int xscore_uefi_update_capsule(struct efi_capsule_header **capsules,
                               uint64_t capsule_count, uint64_t sg_list);
int xscore_uefi_secure_boot_enabled(void);

/* Legacy BIOS Interfaces */
int xscore_bios_thunk(uint16_t interrupt, struct bios_regs *regs);
const struct bios_data_area *xscore_bios_get_bda(void);
uint64_t xscore_bios_get_ebda_address(void);

/* Device Driver & Telemetry Formatters */
int xscore_efivar_init(void);
size_t xscore_efivar_format_proc_list(char *buf, size_t max_size);

#endif /* XSCORE_FIRMWARE_H */
