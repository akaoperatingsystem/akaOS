#include <xscore/acpi.h>
#include <xscore/serial.h>

void *memset(void *destination, int value, size_t count);

static struct xscore_acpi_info g_acpi;
static int g_acpi_initialized = 0;

#if defined(__x86_64__) || defined(__i386__)
static inline __attribute__((unused)) void acpi_out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline __attribute__((unused)) void acpi_out16(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline __attribute__((unused)) uint8_t acpi_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline __attribute__((unused)) uint16_t acpi_in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
#else
static inline __attribute__((unused)) void acpi_out8(uint16_t port, uint8_t value) { (void)port; (void)value; }
static inline __attribute__((unused)) void acpi_out16(uint16_t port, uint16_t value) { (void)port; (void)value; }
static inline __attribute__((unused)) uint8_t acpi_in8(uint16_t port) { (void)port; return 0; }
static inline __attribute__((unused)) uint16_t acpi_in16(uint16_t port) { (void)port; return 0; }
#endif

static int bytes_equal(const void *a, const void *b, size_t n) {
    const uint8_t *p1 = (const uint8_t *)a;
    const uint8_t *p2 = (const uint8_t *)b;
    for (size_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) return 0;
    }
    return 1;
}

static int acpi_checksum(const void *ptr, size_t length) {
    if (!ptr || length == 0) return 0;
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum == 0;
}

#if defined(__x86_64__) || defined(__i386__)
static const struct acpi_rsdp *scan_memory_for_rsdp(void) {
    /* 1. Search EBDA range 0x80000 - 0x9FFFF (16-byte boundary) */
    const char *ebda_mem = (const char *)0x00080000;
    for (size_t off = 0; off < 0x20000; off += 16) {
        if (ebda_mem[off] == 'R' &&
            bytes_equal(ebda_mem + off, "RSD PTR ", 8) &&
            acpi_checksum(ebda_mem + off, 20)) {
            return (const struct acpi_rsdp *)(ebda_mem + off);
        }
    }
    /* 2. Search main BIOS Read-Only Memory area 0xE0000 to 0xFFFFF */
    const char *bios_mem = (const char *)0x000e0000;
    for (size_t off = 0; off < 0x20000; off += 16) {
        if (bios_mem[off] == 'R' &&
            bytes_equal(bios_mem + off, "RSD PTR ", 8) &&
            acpi_checksum(bios_mem + off, 20)) {
            return (const struct acpi_rsdp *)(bios_mem + off);
        }
    }
    return 0;
}
#else
static const struct acpi_rsdp *scan_memory_for_rsdp(void) { return 0; }
#endif

static void parse_dsdt_sleep_states(uint64_t dsdt_paddr) {
    if (!dsdt_paddr) return;
    const struct acpi_sdt_header *dsdt = (const struct acpi_sdt_header *)(uintptr_t)dsdt_paddr;
    if (!dsdt || dsdt->length < sizeof(struct acpi_sdt_header)) return;

    const uint8_t *aml = (const uint8_t *)dsdt;
    size_t len = dsdt->length;

    for (size_t i = 36; i + 8 < len; ++i) {
        if (aml[i] == '_' && aml[i + 1] == 'S' && aml[i + 3] == '_') {
            char state_char = (char)aml[i + 2];
            size_t p = i + 4;
            while (p + 4 < len && aml[p] != 0x12 && p < i + 12) p++;
            if (aml[p] == 0x12) {
                p++;
                uint8_t pkg_lead = aml[p];
                if ((pkg_lead & 0xC0) == 0) p += 1;
                else p += ((pkg_lead >> 6) & 3) + 1;
                p++;
                uint8_t typ_a = 0, typ_b = 0;
                if (p < len) {
                    if (aml[p] == 0x0a) typ_a = aml[p + 1];
                    else typ_a = aml[p];
                }
                p += (aml[p] == 0x0a) ? 2 : 1;
                if (p < len) {
                    if (aml[p] == 0x0a) typ_b = aml[p + 1];
                    else typ_b = aml[p];
                }

                if (state_char == '5') {
                    g_acpi.slp_typa = typ_a;
                    g_acpi.slp_typb = typ_b;
                } else if (state_char == '3') {
                    g_acpi.slp_typa_s3 = typ_a;
                    g_acpi.slp_typb_s3 = typ_b;
                } else if (state_char == '4') {
                    g_acpi.slp_typa_s4 = typ_a;
                    g_acpi.slp_typb_s4 = typ_b;
                }
            }
        }
    }
}

static void parse_fadt(const struct acpi_fadt *fadt) {
    g_acpi.fadt_present = 1;
    g_acpi.pm1a_cnt_blk = (uint16_t)fadt->pm1a_cnt_blk;
    g_acpi.pm1b_cnt_blk = (uint16_t)fadt->pm1b_cnt_blk;
    g_acpi.pm_tmr_blk = (uint16_t)fadt->pm_tmr_blk;
    g_acpi.sci_int = fadt->sci_int;
    g_acpi.smi_cmd = fadt->smi_cmd;
    g_acpi.acpi_enable = fadt->acpi_enable;

    /* DSDT Pointer */
    if (fadt->header.revision >= 2 && fadt->x_dsdt != 0) {
        g_acpi.dsdt_address = fadt->x_dsdt;
    } else {
        g_acpi.dsdt_address = fadt->dsdt;
    }

    /* Reset register info (ACPI 2.0+) */
    if (fadt->header.revision >= 2 && (fadt->flags & (1 << 10))) {
        g_acpi.reset_supported = 1;
        g_acpi.reset_value = fadt->reset_value;
        g_acpi.reset_address = fadt->reset_reg.address;
        g_acpi.reset_space_id = fadt->reset_reg.address_space_id;
    }

    /* Default QEMU / PC sleep type fallback */
    g_acpi.slp_typa = 5;
    g_acpi.slp_typb = 5;
    g_acpi.slp_typa_s3 = 3;
    g_acpi.slp_typb_s3 = 3;
    g_acpi.slp_typa_s4 = 4;
    g_acpi.slp_typb_s4 = 4;

    /* Parse DSDT for exact _S5 sleep values if available */
    if (g_acpi.dsdt_address) {
        parse_dsdt_sleep_states(g_acpi.dsdt_address);
    }
}

static void parse_madt(const struct acpi_madt *madt) {
    g_acpi.madt_present = 1;
    g_acpi.lapic_address = madt->lapic_address;

    const uint8_t *ptr = (const uint8_t *)madt + sizeof(struct acpi_madt);
    const uint8_t *end = (const uint8_t *)madt + madt->header.length;

    while (ptr + sizeof(struct acpi_madt_entry_header) <= end) {
        const struct acpi_madt_entry_header *entry = (const struct acpi_madt_entry_header *)ptr;
        if (entry->length < sizeof(struct acpi_madt_entry_header) || ptr + entry->length > end) {
            break;
        }

        switch (entry->type) {
        case ACPI_MADT_TYPE_LOCAL_APIC: {
            const struct acpi_madt_lapic *lapic = (const struct acpi_madt_lapic *)ptr;
            if ((lapic->flags & 1) && g_acpi.cpu_count < ACPI_MAX_CPUS) {
                g_acpi.cpu_apic_ids[g_acpi.cpu_count++] = lapic->apic_id;
            }
            break;
        }
        case ACPI_MADT_TYPE_IO_APIC: {
            const struct acpi_madt_ioapic *ioapic = (const struct acpi_madt_ioapic *)ptr;
            if (g_acpi.ioapic_count < ACPI_MAX_IOAPIC) {
                g_acpi.ioapic_addresses[g_acpi.ioapic_count] = ioapic->ioapic_address;
                g_acpi.ioapic_gsi_bases[g_acpi.ioapic_count] = ioapic->gsi_base;
                g_acpi.ioapic_count++;
            }
            break;
        }
        case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE: {
            const struct acpi_madt_iso *iso = (const struct acpi_madt_iso *)ptr;
            if (g_acpi.iso_count < ACPI_MAX_ISOS) {
                g_acpi.iso_source[g_acpi.iso_count] = iso->source;
                g_acpi.iso_gsi[g_acpi.iso_count] = iso->gsi;
                g_acpi.iso_flags[g_acpi.iso_count] = iso->flags;
                g_acpi.iso_count++;
            }
            break;
        }
        case ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE: {
            const uint64_t *lapic64 = (const uint64_t *)(ptr + 4);
            g_acpi.lapic_address = (uint32_t)*lapic64;
            break;
        }
        default:
            break;
        }
        ptr += entry->length;
    }
}

static void register_table(const struct acpi_sdt_header *header, uint64_t paddr) {
    if (!header || g_acpi.table_count >= ACPI_MAX_TABLES) return;

    char sig[5];
    for (int i = 0; i < 4; ++i) sig[i] = header->signature[i];
    sig[4] = '\0';

    for (int i = 0; i < 4; ++i) {
        g_acpi.table_signatures[g_acpi.table_count][i] = sig[i];
    }
    g_acpi.table_signatures[g_acpi.table_count][4] = '\0';
    g_acpi.table_addresses[g_acpi.table_count] = paddr;
    g_acpi.table_count++;

    if (bytes_equal(sig, "FACP", 4)) {
        parse_fadt((const struct acpi_fadt *)header);
    } else if (bytes_equal(sig, "APIC", 4)) {
        parse_madt((const struct acpi_madt *)header);
    } else if (bytes_equal(sig, "HPET", 4)) {
        const struct acpi_hpet *hpet = (const struct acpi_hpet *)header;
        g_acpi.hpet_present = 1;
        g_acpi.hpet_address = hpet->address.address;
    } else if (bytes_equal(sig, "MCFG", 4)) {
        const struct acpi_mcfg_entry *mcfg_e =
            (const struct acpi_mcfg_entry *)((const uint8_t *)header + sizeof(struct acpi_mcfg));
        g_acpi.mcfg_present = 1;
        g_acpi.mcfg_address = mcfg_e->base_address;
    }
}

void xscore_acpi_initialize(const struct akaos_boot_info *boot_info) {
    if (g_acpi_initialized) return;
    g_acpi_initialized = 1;
    memset(&g_acpi, 0, sizeof(g_acpi));

    const struct acpi_rsdp *rsdp = 0;

    /* 1. Check UEFI-passed RSDP pointer */
    if (boot_info && boot_info->acpi_rsdp) {
        rsdp = (const struct acpi_rsdp *)(uintptr_t)boot_info->acpi_rsdp;
        if (!bytes_equal(rsdp->signature, "RSD PTR ", 8) || !acpi_checksum(rsdp, 20)) {
            rsdp = 0;
        }
    }

    /* 2. Fallback: Search EBDA and BIOS memory on x86 */
    if (!rsdp) {
        rsdp = scan_memory_for_rsdp();
    }

    if (!rsdp) {
        xscore_serial_write("[INFO] ACPI tables not detected; power management fallbacks active\n");
        return;
    }

    g_acpi.present = 1;
    g_acpi.revision = rsdp->revision;
    g_acpi.rsdp_paddr = (uint64_t)(uintptr_t)rsdp;
    for (int i = 0; i < 6; ++i) g_acpi.oem_id[i] = rsdp->oem_id[i];
    g_acpi.oem_id[6] = '\0';

    /* 3. Prefer XSDT (64-bit pointers) on 64-bit platforms with ACPI 2.0+ */
#if __SIZEOF_POINTER__ == 8
    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        const struct acpi_sdt_header *xsdt =
            (const struct acpi_sdt_header *)(uintptr_t)rsdp->xsdt_address;
        if (xsdt && bytes_equal(xsdt->signature, "XSDT", 4) && acpi_checksum(xsdt, xsdt->length)) {
            g_acpi.xsdt_paddr = rsdp->xsdt_address;
            size_t count = (xsdt->length - sizeof(struct acpi_sdt_header)) / sizeof(uint64_t);
            const uint64_t *ptrs = (const uint64_t *)((const uint8_t *)xsdt + sizeof(struct acpi_sdt_header));
            for (size_t i = 0; i < count; ++i) {
                uint64_t addr = ptrs[i];
                if (!addr) continue;
                const struct acpi_sdt_header *tbl = (const struct acpi_sdt_header *)(uintptr_t)addr;
                if (tbl && acpi_checksum(tbl, tbl->length)) {
                    register_table(tbl, addr);
                }
            }
        }
    }
#endif

    /* 4. Use RSDT (32-bit pointers) if XSDT was absent or produced 0 tables */
    if (g_acpi.table_count == 0 && rsdp->rsdt_address != 0 && rsdp->rsdt_address < 0x10000000U) {
        const struct acpi_sdt_header *rsdt =
            (const struct acpi_sdt_header *)(uintptr_t)rsdp->rsdt_address;
        if (rsdt && bytes_equal(rsdt->signature, "RSDT", 4) && acpi_checksum(rsdt, rsdt->length)) {
            g_acpi.rsdt_paddr = rsdp->rsdt_address;
            size_t count = (rsdt->length - sizeof(struct acpi_sdt_header)) / sizeof(uint32_t);
            const uint32_t *ptrs = (const uint32_t *)((const uint8_t *)rsdt + sizeof(struct acpi_sdt_header));
            for (size_t i = 0; i < count; ++i) {
                uint32_t addr = ptrs[i];
                if (!addr || addr >= 0x10000000U) continue;
                const struct acpi_sdt_header *tbl = (const struct acpi_sdt_header *)(uintptr_t)addr;
                if (tbl && acpi_checksum(tbl, tbl->length)) {
                    register_table(tbl, (uint64_t)addr);
                }
            }
        }
    }

    xscore_serial_write("[INFO] ACPI initialized (OEM: ");
    xscore_serial_write(g_acpi.oem_id);
    xscore_serial_write(", Rev: ");
    char rev_buf[4];
    rev_buf[0] = (char)('0' + g_acpi.revision);
    rev_buf[1] = '\0';
    xscore_serial_write(rev_buf);
    xscore_serial_write(")\n");
}

int xscore_acpi_present(void) {
    return g_acpi.present;
}

const struct xscore_acpi_info *xscore_acpi_get_info(void) {
    return &g_acpi;
}

int xscore_acpi_sleep(int state) {
    if (state == 5) {
        xscore_acpi_shutdown();
        return 1;
    }
    if (state == 3) {
        if (g_acpi.fadt_present && g_acpi.pm1a_cnt_blk != 0) {
            if (g_acpi.smi_cmd && g_acpi.acpi_enable) {
                acpi_out8((uint16_t)g_acpi.smi_cmd, g_acpi.acpi_enable);
            }
            uint16_t val_a = (uint16_t)(((uint16_t)g_acpi.slp_typa_s3 << 10) | (1U << 13));
            acpi_out16(g_acpi.pm1a_cnt_blk, val_a);
            if (g_acpi.pm1b_cnt_blk != 0) {
                uint16_t val_b = (uint16_t)(((uint16_t)g_acpi.slp_typb_s3 << 10) | (1U << 13));
                acpi_out16(g_acpi.pm1b_cnt_blk, val_b);
            }
            return 1;
        }
        /* Fallback: QEMU / PC standby */
        acpi_out16(0x604, 0x1400);
        return 1;
    }
    if (state == 4) {
        if (g_acpi.fadt_present && g_acpi.pm1a_cnt_blk != 0) {
            uint16_t val_a = (uint16_t)(((uint16_t)g_acpi.slp_typa_s4 << 10) | (1U << 13));
            acpi_out16(g_acpi.pm1a_cnt_blk, val_a);
            return 1;
        }
    }
    return 0;
}

void xscore_acpi_shutdown(void) {
    if (g_acpi.fadt_present && g_acpi.pm1a_cnt_blk != 0) {
        /* If system requires ACPI enable command, issue it */
        if (g_acpi.smi_cmd && g_acpi.acpi_enable) {
            acpi_out8((uint16_t)g_acpi.smi_cmd, g_acpi.acpi_enable);
        }
        /* Write SLP_TYPa | SLP_EN (Bit 13 = 0x2000) to PM1a control block */
        uint16_t val_a = (uint16_t)(((uint16_t)g_acpi.slp_typa << 10) | (1U << 13));
        acpi_out16(g_acpi.pm1a_cnt_blk, val_a);

        if (g_acpi.pm1b_cnt_blk != 0) {
            uint16_t val_b = (uint16_t)(((uint16_t)g_acpi.slp_typb << 10) | (1U << 13));
            acpi_out16(g_acpi.pm1b_cnt_blk, val_b);
        }
    }

    /* Universal fallback I/O ports for hypervisors and virtual machines */
    acpi_out16(0x604, 0x2000);   /* QEMU Q35 PM1a_CNT */
    acpi_out16(0xb004, 0x2000);  /* QEMU PIIX4 / Bochs */
    acpi_out16(0x4004, 0x3400);  /* VirtualBox */
    acpi_out8(0xf4, 0x00);       /* QEMU isa-debug-exit */
}

void xscore_acpi_reboot(void) {
    if (g_acpi.reset_supported && g_acpi.reset_address != 0) {
        if (g_acpi.reset_space_id == 1) {
            /* System I/O */
            acpi_out8((uint16_t)g_acpi.reset_address, g_acpi.reset_value);
        } else if (g_acpi.reset_space_id == 0) {
            /* System Memory */
            *(volatile uint8_t *)(uintptr_t)g_acpi.reset_address = g_acpi.reset_value;
        }
    }

    /* Universal fallback resets: 8042 keyboard pulse, PCI reset */
    acpi_out8(0x64, 0xfe);
    acpi_out8(0xcf9, 0x06);
}

/* ================================================================
 * ProcFS Telemetry Formatters
 * ================================================================ */

static size_t str_append_at(char *buf, size_t max, size_t off, const char *s) {
    if (off >= max) return off;
    while (*s && off + 1 < max) buf[off++] = *s++;
    buf[off] = '\0';
    return off;
}

static size_t num_append_at(char *buf, size_t max, size_t off, uint64_t val) {
    char tmp[32];
    int pos = 0;
    if (val == 0) tmp[pos++] = '0';
    else {
        while (val > 0) {
            tmp[pos++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    while (pos > 0 && off + 1 < max) {
        buf[off++] = tmp[--pos];
    }
    buf[off] = '\0';
    return off;
}

static size_t hex_append_at(char *buf, size_t max, size_t off, uint64_t val, int digits) {
    static const char hex[] = "0123456789abcdef";
    off = str_append_at(buf, max, off, "0x");
    for (int i = (digits - 1) * 4; i >= 0; i -= 4) {
        if (off + 1 < max) {
            buf[off++] = hex[(val >> i) & 0xf];
        }
    }
    buf[off] = '\0';
    return off;
}

size_t xscore_acpi_format_proc_info(char *buf, size_t max_size) {
    if (!buf || max_size == 0) return 0;
    buf[0] = '\0';
    size_t off = 0;

    off = str_append_at(buf, max_size, off, "=== akaOS ACPI Subsystem Information ===\n");
    off = str_append_at(buf, max_size, off, "Status       : ");
    off = str_append_at(buf, max_size, off, g_acpi.present ? "Present / Active\n" : "Not Present / Inactive\n");
    if (!g_acpi.present) return off;

    off = str_append_at(buf, max_size, off, "OEM ID       : ");
    off = str_append_at(buf, max_size, off, g_acpi.oem_id);
    off = str_append_at(buf, max_size, off, "\nRevision     : ");
    off = num_append_at(buf, max_size, off, g_acpi.revision);
    off = str_append_at(buf, max_size, off, "\nRSDP Address : ");
    off = hex_append_at(buf, max_size, off, g_acpi.rsdp_paddr, 8);
    if (g_acpi.xsdt_paddr) {
        off = str_append_at(buf, max_size, off, "\nXSDT Address : ");
        off = hex_append_at(buf, max_size, off, g_acpi.xsdt_paddr, 8);
    } else if (g_acpi.rsdt_paddr) {
        off = str_append_at(buf, max_size, off, "\nRSDT Address : ");
        off = hex_append_at(buf, max_size, off, g_acpi.rsdt_paddr, 8);
    }
    off = str_append_at(buf, max_size, off, "\nDiscovered Tables (");
    off = num_append_at(buf, max_size, off, g_acpi.table_count);
    off = str_append_at(buf, max_size, off, "):\n");

    for (uint32_t i = 0; i < g_acpi.table_count; ++i) {
        off = str_append_at(buf, max_size, off, "  [");
        off = num_append_at(buf, max_size, off, i);
        off = str_append_at(buf, max_size, off, "] ");
        off = str_append_at(buf, max_size, off, g_acpi.table_signatures[i]);
        off = str_append_at(buf, max_size, off, " at ");
        off = hex_append_at(buf, max_size, off, g_acpi.table_addresses[i], 8);
        off = str_append_at(buf, max_size, off, "\n");
    }

    if (g_acpi.hpet_present) {
        off = str_append_at(buf, max_size, off, "HPET MMIO    : ");
        off = hex_append_at(buf, max_size, off, g_acpi.hpet_address, 8);
        off = str_append_at(buf, max_size, off, "\n");
    }
    if (g_acpi.mcfg_present) {
        off = str_append_at(buf, max_size, off, "PCIe ECAM    : ");
        off = hex_append_at(buf, max_size, off, g_acpi.mcfg_address, 8);
        off = str_append_at(buf, max_size, off, "\n");
    }
    return off;
}

size_t xscore_acpi_format_proc_fadt(char *buf, size_t max_size) {
    if (!buf || max_size == 0) return 0;
    buf[0] = '\0';
    size_t off = 0;

    off = str_append_at(buf, max_size, off, "=== Fixed ACPI Description Table (FADT) ===\n");
    if (!g_acpi.fadt_present) {
        off = str_append_at(buf, max_size, off, "FADT not found.\n");
        return off;
    }

    off = str_append_at(buf, max_size, off, "PM1a Control Block : ");
    off = hex_append_at(buf, max_size, off, g_acpi.pm1a_cnt_blk, 4);
    off = str_append_at(buf, max_size, off, "\nPM1b Control Block : ");
    off = hex_append_at(buf, max_size, off, g_acpi.pm1b_cnt_blk, 4);
    off = str_append_at(buf, max_size, off, "\nPM Timer Block     : ");
    off = hex_append_at(buf, max_size, off, g_acpi.pm_tmr_blk, 4);
    off = str_append_at(buf, max_size, off, "\nSCI Interrupt      : IRQ ");
    off = num_append_at(buf, max_size, off, g_acpi.sci_int);
    off = str_append_at(buf, max_size, off, "\nSMI Command Port   : ");
    off = hex_append_at(buf, max_size, off, g_acpi.smi_cmd, 4);
    off = str_append_at(buf, max_size, off, " (Enable: ");
    off = hex_append_at(buf, max_size, off, g_acpi.acpi_enable, 2);
    off = str_append_at(buf, max_size, off, ")\nDSDT Address       : ");
    off = hex_append_at(buf, max_size, off, g_acpi.dsdt_address, 8);
    off = str_append_at(buf, max_size, off, "\nS5 Sleep Type (a/b): ");
    off = num_append_at(buf, max_size, off, g_acpi.slp_typa);
    off = str_append_at(buf, max_size, off, " / ");
    off = num_append_at(buf, max_size, off, g_acpi.slp_typb);
    off = str_append_at(buf, max_size, off, "\nReset Supported    : ");
    off = str_append_at(buf, max_size, off, g_acpi.reset_supported ? "Yes" : "No");
    if (g_acpi.reset_supported) {
        off = str_append_at(buf, max_size, off, " (Reg: ");
        off = hex_append_at(buf, max_size, off, g_acpi.reset_address, 4);
        off = str_append_at(buf, max_size, off, ", Val: ");
        off = hex_append_at(buf, max_size, off, g_acpi.reset_value, 2);
        off = str_append_at(buf, max_size, off, ")");
    }
    off = str_append_at(buf, max_size, off, "\n");
    return off;
}

size_t xscore_acpi_format_proc_madt(char *buf, size_t max_size) {
    if (!buf || max_size == 0) return 0;
    buf[0] = '\0';
    size_t off = 0;

    off = str_append_at(buf, max_size, off, "=== Multiple APIC Description Table (MADT) ===\n");
    if (!g_acpi.madt_present) {
        off = str_append_at(buf, max_size, off, "MADT not found.\n");
        return off;
    }

    off = str_append_at(buf, max_size, off, "Local APIC Base    : ");
    off = hex_append_at(buf, max_size, off, g_acpi.lapic_address, 8);
    off = str_append_at(buf, max_size, off, "\nEnabled CPU Cores  : ");
    off = num_append_at(buf, max_size, off, g_acpi.cpu_count);
    off = str_append_at(buf, max_size, off, "\n");

    for (uint32_t i = 0; i < g_acpi.cpu_count; ++i) {
        off = str_append_at(buf, max_size, off, "  CPU Core ");
        off = num_append_at(buf, max_size, off, i);
        off = str_append_at(buf, max_size, off, ": APIC ID ");
        off = num_append_at(buf, max_size, off, g_acpi.cpu_apic_ids[i]);
        off = str_append_at(buf, max_size, off, "\n");
    }

    off = str_append_at(buf, max_size, off, "I/O APICs (");
    off = num_append_at(buf, max_size, off, g_acpi.ioapic_count);
    off = str_append_at(buf, max_size, off, "):\n");
    for (uint32_t i = 0; i < g_acpi.ioapic_count; ++i) {
        off = str_append_at(buf, max_size, off, "  IOAPIC ");
        off = num_append_at(buf, max_size, off, i);
        off = str_append_at(buf, max_size, off, ": MMIO ");
        off = hex_append_at(buf, max_size, off, g_acpi.ioapic_addresses[i], 8);
        off = str_append_at(buf, max_size, off, ", GSI Base: ");
        off = num_append_at(buf, max_size, off, g_acpi.ioapic_gsi_bases[i]);
        off = str_append_at(buf, max_size, off, "\n");
    }

    off = str_append_at(buf, max_size, off, "Interrupt Source Overrides (");
    off = num_append_at(buf, max_size, off, g_acpi.iso_count);
    off = str_append_at(buf, max_size, off, "):\n");
    for (uint32_t i = 0; i < g_acpi.iso_count; ++i) {
        off = str_append_at(buf, max_size, off, "  ISA IRQ ");
        off = num_append_at(buf, max_size, off, g_acpi.iso_source[i]);
        off = str_append_at(buf, max_size, off, " -> GSI ");
        off = num_append_at(buf, max_size, off, g_acpi.iso_gsi[i]);
        off = str_append_at(buf, max_size, off, " (Flags: ");
        off = hex_append_at(buf, max_size, off, g_acpi.iso_flags[i], 4);
        off = str_append_at(buf, max_size, off, ")\n");
    }
    return off;
}
