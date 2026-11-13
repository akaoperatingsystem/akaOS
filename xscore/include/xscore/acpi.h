#ifndef XSCORE_ACPI_H
#define XSCORE_ACPI_H

#include <stddef.h>
#include <stdint.h>
#include <akaos/boot_info.h>

#define ACPI_MAX_TABLES 16
#define ACPI_MAX_CPUS   16
#define ACPI_MAX_IOAPIC 4
#define ACPI_MAX_ISOS   16

/* ACPI 1.0 / 2.0+ Root System Description Pointer */
struct acpi_rsdp {
    char signature[8];          /* "RSD PTR " */
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;            /* 0 = ACPI 1.0, 2 = ACPI 2.0+ */
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

/* Standard ACPI Description Table Header */
struct acpi_sdt_header {
    char signature[4];          /* e.g. "FACP", "APIC", "HPET", "MCFG" */
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

/* Generic Address Structure (GAS) */
struct acpi_gas {
    uint8_t address_space_id;   /* 0 = Memory, 1 = I/O */
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed));

/* Fixed ACPI Description Table (FADT) */
struct acpi_fadt {
    struct acpi_sdt_header header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_cnt;
    uint32_t pm1a_evt_blk;
    uint32_t pm1b_evt_blk;
    uint32_t pm1a_cnt_blk;
    uint32_t pm1b_cnt_blk;
    uint32_t pm2_cnt_blk;
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t pm1_evt_len;
    uint8_t pm1_cnt_len;
    uint8_t pm2_cnt_len;
    uint8_t pm_tmr_len;
    uint8_t gpe0_blk_len;
    uint8_t gpe1_blk_len;
    uint8_t gpe1_base;
    uint8_t cst_cnt;
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alrm;
    uint8_t mon_alrm;
    uint8_t century;
    uint16_t iapc_boot_arch;
    uint8_t reserved2;
    uint32_t flags;
    struct acpi_gas reset_reg;
    uint8_t reset_value;
    uint8_t reserved3[3];
    uint64_t x_firmware_ctrl;
    uint64_t x_dsdt;
    struct acpi_gas x_pm1a_evt_blk;
    struct acpi_gas x_pm1b_evt_blk;
    struct acpi_gas x_pm1a_cnt_blk;
    struct acpi_gas x_pm1b_cnt_blk;
    struct acpi_gas x_pm2_cnt_blk;
    struct acpi_gas x_pm_tmr_blk;
    struct acpi_gas x_gpe0_blk;
    struct acpi_gas x_gpe1_blk;
} __attribute__((packed));

/* Multiple APIC Description Table (MADT) */
struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t lapic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

#define ACPI_MADT_TYPE_LOCAL_APIC          0
#define ACPI_MADT_TYPE_IO_APIC             1
#define ACPI_MADT_TYPE_INTERRUPT_OVERRIDE  2
#define ACPI_MADT_TYPE_NMI_SOURCE          4
#define ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE 5

struct acpi_madt_lapic {
    struct acpi_madt_entry_header header;
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags; /* Bit 0: Enabled */
} __attribute__((packed));

struct acpi_madt_ioapic {
    struct acpi_madt_entry_header header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t gsi_base;
} __attribute__((packed));

struct acpi_madt_iso {
    struct acpi_madt_entry_header header;
    uint8_t bus;     /* 0 = ISA */
    uint8_t source;  /* ISA IRQ */
    uint32_t gsi;    /* Global System Interrupt */
    uint16_t flags;  /* Polarity and trigger mode */
} __attribute__((packed));

/* HPET Description Table */
struct acpi_hpet {
    struct acpi_sdt_header header;
    uint32_t event_timer_block_id;
    struct acpi_gas address;
    uint8_t hpet_number;
    uint16_t main_counter_min_tick;
    uint8_t page_protection_oem;
} __attribute__((packed));

/* MCFG Description Table */
struct acpi_mcfg_entry {
    uint64_t base_address;
    uint16_t pci_segment_group_number;
    uint8_t start_pci_bus_number;
    uint8_t end_pci_bus_number;
    uint32_t reserved;
} __attribute__((packed));

struct acpi_mcfg {
    struct acpi_sdt_header header;
    uint64_t reserved;
} __attribute__((packed));

/* Parsed Kernel ACPI State */
struct xscore_acpi_info {
    int present;
    uint8_t revision;
    char oem_id[7];
    uint64_t rsdp_paddr;
    uint64_t rsdt_paddr;
    uint64_t xsdt_paddr;

    /* Discovered Tables */
    uint32_t table_count;
    char table_signatures[ACPI_MAX_TABLES][5];
    uint64_t table_addresses[ACPI_MAX_TABLES];

    /* FADT & Power Management */
    int fadt_present;
    uint16_t pm1a_cnt_blk;
    uint16_t pm1b_cnt_blk;
    uint16_t pm_tmr_blk;
    uint64_t dsdt_address;
    uint8_t slp_typa;
    uint8_t slp_typb;
    uint8_t slp_typa_s3;
    uint8_t slp_typb_s3;
    uint8_t slp_typa_s4;
    uint8_t slp_typb_s4;
    uint8_t reset_supported;
    uint8_t reset_value;
    uint64_t reset_address;
    uint8_t reset_space_id;
    uint16_t sci_int;
    uint32_t smi_cmd;
    uint8_t acpi_enable;

    /* MADT & Processor/Interrupt Topology */
    int madt_present;
    uint32_t lapic_address;
    uint32_t cpu_count;
    uint8_t cpu_apic_ids[ACPI_MAX_CPUS];
    uint32_t ioapic_count;
    uint32_t ioapic_addresses[ACPI_MAX_IOAPIC];
    uint32_t ioapic_gsi_bases[ACPI_MAX_IOAPIC];
    uint32_t iso_count;
    uint8_t iso_source[ACPI_MAX_ISOS];
    uint32_t iso_gsi[ACPI_MAX_ISOS];
    uint16_t iso_flags[ACPI_MAX_ISOS];

    /* HPET & MCFG */
    int hpet_present;
    uint64_t hpet_address;
    int mcfg_present;
    uint64_t mcfg_address;
};

void xscore_acpi_initialize(const struct akaos_boot_info *boot_info);
int xscore_acpi_present(void);
const struct xscore_acpi_info *xscore_acpi_get_info(void);

void xscore_acpi_shutdown(void);
void xscore_acpi_reboot(void);
int xscore_acpi_sleep(int state);

size_t xscore_acpi_format_proc_info(char *buf, size_t max_size);
size_t xscore_acpi_format_proc_fadt(char *buf, size_t max_size);
size_t xscore_acpi_format_proc_madt(char *buf, size_t max_size);

#endif /* XSCORE_ACPI_H */
