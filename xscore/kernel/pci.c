#include <xscore/pci.h>
#include <xscore/serial.h>
#include <xscore/vmm.h>
#include <xscore/apic.h>
#include <xscore/smp.h>
#include <stddef.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS UINT16_C(0x0cf8)
#define PCI_CONFIG_DATA    UINT16_C(0x0cfc)
#define PCI_CONFIG_ENABLE  UINT32_C(0x80000000)
#define PCI_HEADER_MULTI_FUNCTION UINT8_C(0x80)

static struct xscore_pci_device devices[XSCORE_PCI_MAX_DEVICES];
static uint32_t device_count;

#if defined(__x86_64__) || defined(__i386__)
static inline void port_out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t port_in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
#else
static inline void port_out32(uint16_t port, uint32_t value) { (void)port; (void)value; }
static inline uint32_t port_in32(uint16_t port) { (void)port; return 0xffffffff; }
#endif

static void pci_config_write32(uint8_t bus, uint8_t slot,
                               uint8_t function, uint8_t offset,
                               uint32_t value) {
    uint32_t address = PCI_CONFIG_ENABLE |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       (offset & UINT8_C(0xfc));
    port_out32(PCI_CONFIG_ADDRESS, address);
    port_out32(PCI_CONFIG_DATA, value);
}

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot,
                                  uint8_t function, uint8_t offset) {
    uint32_t address = PCI_CONFIG_ENABLE |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       (offset & UINT8_C(0xfc));
    port_out32(PCI_CONFIG_ADDRESS, address);
    return port_in32(PCI_CONFIG_DATA);
}

uint32_t xscore_pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    return pci_config_read32(bus, slot, function, offset);
}

void xscore_pci_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    pci_config_write32(bus, slot, function, offset, value);
}

int xscore_pci_enable_msix(const struct xscore_pci_device *dev, uint8_t vector) {
#if defined(__x86_64__)
    if (!dev || !dev->msix_cap_offset || vector < 32 || vector >= 48) return 0;
    uint8_t cap = dev->msix_cap_offset;
    uint32_t control = xscore_pci_read32(dev->bus, dev->slot, dev->function, cap);
    uint32_t table = xscore_pci_read32(dev->bus, dev->slot, dev->function, cap + 4);
    uint32_t bar = table & 7, offset = table & ~7U;
    if (bar >= 6 || dev->bar_is_io[bar] || !dev->bars[bar] ||
        dev->bar_sizes[bar] < 16 || offset > dev->bar_sizes[bar] - 16) return 0;
    uint64_t address = dev->bars[bar] + offset;
    uint64_t page = address & ~UINT64_C(4095);
    uint64_t flags = XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE | UINT64_C(0x18);
    if (xscore_vmm_is_mapped(page, NULL)) {
        if (!xscore_vmm_split_large_page(page & ~UINT64_C(0x1fffff)) ||
            !xscore_vmm_update_page_flags(page, flags)) return 0;
    } else if (!xscore_vmm_map_page(page, page, flags)) return 0;
    uintptr_t saved = xscore_irq_save();
    /* Function mask protects the table until entry zero is fully published. */
    xscore_pci_write32(dev->bus, dev->slot, dev->function, cap, control | (3U << 30));
    volatile uint32_t *entry = (volatile uint32_t *)(uintptr_t)address;
    entry[3] = 1;
    entry[0] = 0xfee00000U | (xscore_apic_id() << 12);
    entry[1] = 0;
    entry[2] = vector;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    entry[3] = 0;
    (void)entry[3];
    xscore_pci_write32(dev->bus, dev->slot, dev->function, cap,
                      (control | (1U << 31)) & ~(1U << 30));
    xscore_irq_restore(saved);
    return 1;
#else
    (void)dev;
    (void)vector;
    return 0;
#endif
}

static char *append_hex(char *out, uint64_t value, unsigned digits) {
    static const char hex[] = "0123456789abcdef";
    while (digits) {
        --digits;
        *out++ = hex[(value >> (digits * 4)) & 0xf];
    }
    *out = 0;
    return out;
}

static void serial_device(const struct xscore_pci_device *device) {
    char line[112];
    char *out = line;
    const char prefix[] = "[INFO] PCI ";
    const char vendor[] = " vendor=0x";
    const char device_text[] = " device=0x";
    const char class_text[] = " class=0x";
    const char suffix[] = "\n";
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i) *out++ = prefix[i];
    out = append_hex(out, device->bus, 2);
    *out++ = ':';
    out = append_hex(out, device->slot, 2);
    *out++ = '.';
    out = append_hex(out, device->function, 1);
    for (size_t i = 0; i < sizeof(vendor) - 1; ++i) *out++ = vendor[i];
    out = append_hex(out, device->vendor_id, 4);
    for (size_t i = 0; i < sizeof(device_text) - 1; ++i) *out++ = device_text[i];
    out = append_hex(out, device->device_id, 4);
    for (size_t i = 0; i < sizeof(class_text) - 1; ++i) *out++ = class_text[i];
    out = append_hex(out, ((uint16_t)device->class_code << 8) | device->subclass, 4);
    for (size_t i = 0; i < sizeof(suffix) - 1; ++i) *out++ = suffix[i];
    *out = 0;
    xscore_serial_write(line);
}

static void read_bars(struct xscore_pci_device *device) {
    if ((device->header_type & UINT8_C(0x7f)) != 0) return;
    for (uint8_t index = 0; index < 6; ++index) {
        uint8_t bar_offset = (uint8_t)(0x10 + index * 4);
        uint32_t orig_low = pci_config_read32(device->bus, device->slot,
                                             device->function, bar_offset);
        if (orig_low == 0 || orig_low == UINT32_MAX) continue;

        /* Query BAR size */
        pci_config_write32(device->bus, device->slot, device->function, bar_offset, 0xffffffff);
        uint32_t size_mask = pci_config_read32(device->bus, device->slot, device->function, bar_offset);
        pci_config_write32(device->bus, device->slot, device->function, bar_offset, orig_low);

        if (orig_low & 1U) {
            device->bar_is_io[index] = 1;
            device->bars[index] = orig_low & UINT32_C(0xfffffffc);
            uint32_t sz = size_mask & UINT32_C(0xfffffffc);
            device->bar_sizes[index] = sz ? (~sz + 1) : 0;
            continue;
        }
        uint32_t type = (orig_low >> 1) & 3U;
        device->bars[index] = orig_low & UINT32_C(0xfffffff0);
        uint32_t sz = size_mask & UINT32_C(0xfffffff0);
        device->bar_sizes[index] = sz ? (~sz + 1) : 0;

        if (type == 2U && index < 5) {
            uint8_t hi_offset = (uint8_t)(0x10 + (index + 1) * 4);
            uint32_t high = pci_config_read32(device->bus, device->slot,
                                              device->function, hi_offset);
            device->bars[index] |= (uint64_t)high << 32;
            ++index;
        }
    }
}

static void read_capabilities(struct xscore_pci_device *device) {
    uint32_t status_cmd = pci_config_read32(device->bus, device->slot, device->function, 0x04);
    uint16_t status = (uint16_t)(status_cmd >> 16);
    if (!(status & (1 << 4))) return;

    uint32_t cap_reg = pci_config_read32(device->bus, device->slot, device->function, 0x34);
    uint8_t cap_ptr = (uint8_t)(cap_reg & 0xfc);

    int count = 0;
    while (cap_ptr >= 0x40 && count++ < 48) {
        uint32_t cap_hdr = pci_config_read32(device->bus, device->slot, device->function, cap_ptr);
        uint8_t cap_id = (uint8_t)(cap_hdr & 0xff);
        uint8_t next_ptr = (uint8_t)((cap_hdr >> 8) & 0xfc);

        if (cap_id == 0x10) { /* PCI Express */
            device->pcie_cap_offset = cap_ptr;
            uint32_t link_cap = pci_config_read32(device->bus, device->slot, device->function, cap_ptr + 0x0c);
            device->pcie_link_speed = (uint8_t)(link_cap & 0x0f);
            device->pcie_link_width = (uint8_t)((link_cap >> 4) & 0x3f);
        } else if (cap_id == 0x05) { /* MSI */
            device->msi_cap_offset = cap_ptr;
        } else if (cap_id == 0x11) { /* MSI-X */
            device->msix_cap_offset = cap_ptr;
        }

        cap_ptr = next_ptr;
    }
}

uint32_t xscore_pci_initialize(void) {
    device_count = 0;
    for (uint32_t bus = 0; bus < 256; ++bus) {
        for (uint32_t slot = 0; slot < 32; ++slot) {
            uint32_t id = pci_config_read32((uint8_t)bus, (uint8_t)slot, 0, 0);
            if ((id & UINT32_C(0xffff)) == UINT32_C(0xffff)) continue;
            uint8_t header = (uint8_t)(pci_config_read32(
                (uint8_t)bus, (uint8_t)slot, 0, 0x0c) >> 16);
            uint32_t functions = (header & PCI_HEADER_MULTI_FUNCTION) ? 8 : 1;
            for (uint32_t function = 0; function < functions; ++function) {
                id = pci_config_read32((uint8_t)bus, (uint8_t)slot,
                                       (uint8_t)function, 0);
                if ((id & UINT32_C(0xffff)) == UINT32_C(0xffff)) continue;
                if (device_count >= XSCORE_PCI_MAX_DEVICES) continue;
                struct xscore_pci_device *device = &devices[device_count++];
                device->bus = (uint8_t)bus;
                device->slot = (uint8_t)slot;
                device->function = (uint8_t)function;
                device->vendor_id = (uint16_t)(id & UINT32_C(0xffff));
                device->device_id = (uint16_t)(id >> 16);
                uint32_t class_info = pci_config_read32(
                    (uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0x08);
                device->revision = (uint8_t)class_info;
                device->prog_if = (uint8_t)(class_info >> 8);
                device->subclass = (uint8_t)(class_info >> 16);
                device->class_code = (uint8_t)(class_info >> 24);
                device->header_type = (uint8_t)(pci_config_read32(
                    (uint8_t)bus, (uint8_t)slot, (uint8_t)function, 0x0c) >> 16);
                read_bars(device);
                read_capabilities(device);
                serial_device(device);
            }
        }
    }
    xscore_serial_write("[INFO] PCI configuration-space scan complete\n");
    return device_count;
}

uint32_t xscore_pci_device_count(void) {
    return device_count;
}

const struct xscore_pci_device *xscore_pci_get_device(uint32_t index) {
    if (index < device_count) return &devices[index];
    return 0;
}

const struct xscore_pci_device *xscore_pci_find(uint16_t vendor_id,
                                                uint16_t device_id) {
    for (uint32_t index = 0; index < device_count; ++index) {
        if (devices[index].vendor_id == vendor_id &&
            devices[index].device_id == device_id) return &devices[index];
    }
    return 0;
}

const struct xscore_pci_device *xscore_pci_find_class(uint8_t class_code,
                                                      uint8_t subclass) {
    for (uint32_t index = 0; index < device_count; ++index) {
        if (devices[index].class_code == class_code &&
            devices[index].subclass == subclass) return &devices[index];
    }
    return 0;
}

int xscore_pci_enable_bus_master(const struct xscore_pci_device *device) {
    if (!device) return 0;
    uint32_t command_status = pci_config_read32(device->bus, device->slot,
                                                device->function, 0x04);
    uint32_t command_bits = UINT32_C(0x0004);
    for (uint8_t index = 0; index < 6; ++index) {
        if (device->bars[index] == 0) continue;
        command_bits |= device->bar_is_io[index] ? UINT32_C(0x0001)
                                                 : UINT32_C(0x0002);
    }
    if ((command_status & command_bits) != command_bits) {
        pci_config_write32(device->bus, device->slot, device->function, 0x04,
                           command_status | command_bits);
    }
    command_status = pci_config_read32(device->bus, device->slot,
                                        device->function, 0x04);
    return (command_status & command_bits) == command_bits;
}

const char *xscore_pci_vendor_name(uint16_t vendor_id) {
    switch (vendor_id) {
        case 0x8086: return "Intel Corporation";
        case 0x10de: return "NVIDIA Corporation";
        case 0x1002: return "Advanced Micro Devices, Inc. [AMD/ATI]";
        case 0x1af4: return "Red Hat, Inc. (VirtIO)";
        case 0x1234: return "Bochs / QEMU Standard";
        case 0x15ad: return "VMware";
        case 0x10ec: return "Realtek Semiconductor Co., Ltd.";
        case 0x1274: return "Ensoniq / Creative Labs";
        case 0x1106: return "VIA Technologies, Inc.";
        case 0x1013: return "Cirrus Logic";
        default: return "Unknown Vendor";
    }
}

const char *xscore_pci_class_name(uint8_t class_code, uint8_t subclass) {
    uint16_t code = ((uint16_t)class_code << 8) | subclass;
    switch (code) {
        case 0x0001: return "VGA compatible unclassified device";
        case 0x0100: return "SCSI storage controller";
        case 0x0101: return "IDE interface";
        case 0x0106: return "SATA controller (AHCI)";
        case 0x0108: return "Non-Volatile memory controller (NVMe)";
        case 0x0200: return "Ethernet controller";
        case 0x0300: return "VGA compatible controller";
        case 0x0302: return "3D controller";
        case 0x0380: return "Display controller";
        case 0x0401: return "Multimedia audio controller";
        case 0x0403: return "Audio device (HD Audio)";
        case 0x0600: return "Host bridge";
        case 0x0601: return "ISA bridge";
        case 0x0604: return "PCI bridge";
        case 0x0c03: return "USB controller";
        case 0x0c05: return "SMBus controller";
        default: return "Peripheral device";
    }
}

const char *xscore_pci_device_name(uint16_t vendor_id, uint16_t device_id) {
    if (vendor_id == 0x1234 && device_id == 0x1111) return "BGA / Standard VGA Graphics Adapter";
    if (vendor_id == 0x1af4) {
        switch (device_id) {
            case 0x1000: return "VirtIO Network Device";
            case 0x1001: return "VirtIO Block Device";
            case 0x1002: return "VirtIO Memory Balloon";
            case 0x1003: return "VirtIO Console";
            case 0x1050: return "VirtIO GPU Display Adapter";
            case 0x1052: return "VirtIO Input Device";
            case 0x1059: return "VirtIO Sound Device";
            default: return "VirtIO Virtual Device";
        }
    }
    if (vendor_id == 0x8086) {
        switch (device_id) {
            case 0x29c0: return "82G33/G31/P35/P31 Express DRAM Controller";
            case 0x2918: return "82801IB (ICH9) LPC Interface Controller";
            case 0x2922: return "82801IR/IO/IH (ICH9R/DO/DH) 6 port SATA Controller [AHCI mode]";
            case 0x2668: return "82801FB/FBM/FR/FW/FRW (ICH6 Family) High Definition Audio Controller";
            case 0x293e: return "82801I (ICH9 Family) HD Audio Controller";
            case 0x100e: return "82540EM Gigabit Ethernet Controller";
            case 0x10d3: return "82574L Gigabit Network Connection";
            case 0x5690: return "Intel Arc A770 Graphics (Alchemist)";
            case 0x5691: return "Intel Arc A750 Graphics (Alchemist)";
            case 0x56a0: return "Intel Arc A380 Graphics (Alchemist)";
            case 0x56b0: return "Intel Arc Battlemage B580 Graphics";
            case 0x9a49: return "Intel Iris Xe Graphics (Tiger Lake)";
            case 0x4680: return "Intel UHD Graphics 770 (Alder Lake)";
            default: return "Intel System Peripheral";
        }
    }
    if (vendor_id == 0x10de) {
        if (device_id >= 0x2900 && device_id <= 0x29ff) return "GeForce RTX 5090/5080/5070 (Blackwell)";
        if (device_id == 0x2684) return "GeForce RTX 4090 (Ada Lovelace)";
        if (device_id == 0x2704) return "GeForce RTX 4080 (Ada Lovelace)";
        if (device_id == 0x2782) return "GeForce RTX 4070 Ti (Ada Lovelace)";
        if (device_id == 0x2204) return "GeForce RTX 3090 (Ampere)";
        if (device_id == 0x2206) return "GeForce RTX 3080 (Ampere)";
        if (device_id == 0x2484) return "GeForce RTX 3070 (Ampere)";
        if (device_id == 0x1e04) return "GeForce RTX 2080 Ti (Turing)";
        if (device_id == 0x1b80) return "GeForce GTX 1080 (Pascal)";
        return "NVIDIA GeForce / RTX Graphics";
    }
    if (vendor_id == 0x1002) {
        if (device_id >= 0x7500 && device_id <= 0x75ff) return "Radeon RX 9000 Series (RDNA 4)";
        if (device_id == 0x744c) return "Radeon RX 7900 XTX / 7900 XT (Navi 31)";
        if (device_id == 0x747e) return "Radeon RX 7800 XT / 7700 XT (Navi 32)";
        if (device_id == 0x73bf) return "Radeon RX 6900 XT / 6800 XT (Navi 21)";
        if (device_id == 0x731f) return "Radeon RX 5700 XT / 5700 (Navi 10)";
        if (device_id == 0x687f) return "Radeon RX Vega 64 / 56";
        if (device_id == 0x67df) return "Radeon RX 580 / 570 (Polaris)";
        return "AMD Radeon Graphics";
    }
    if (vendor_id == 0x15ad && device_id == 0x0405) return "VMware SVGA II Adapter";
    if (vendor_id == 0x10ec && device_id == 0x8139) return "RTL-8139/8139C/8139C+ Ethernet";
    if (vendor_id == 0x1274 && device_id == 0x1371) return "ES1371 / Creative Labs CT5880 AudioPCI";
    return "PCI Device";
}

static size_t str_append(char *buf, size_t max_size, size_t offset, const char *str) {
    if (offset >= max_size) return offset;
    while (*str && offset + 1 < max_size) {
        buf[offset++] = *str++;
    }
    buf[offset] = '\0';
    return offset;
}

static size_t str_append_hex(char *buf, size_t max_size, size_t offset, uint64_t val, int digits) {
    static const char hex[] = "0123456789abcdef";
    if (offset + digits >= max_size) return offset;
    for (int i = digits - 1; i >= 0; --i) {
        buf[offset++] = hex[(val >> (i * 4)) & 0xf];
    }
    buf[offset] = '\0';
    return offset;
}

static size_t str_append_u32(char *buf, size_t max_size, size_t offset, uint32_t val) {
    char tmp[12];
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    else {
        while (val > 0) {
            tmp[i++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    while (i > 0 && offset + 1 < max_size) {
        buf[offset++] = tmp[--i];
    }
    buf[offset] = '\0';
    return offset;
}

size_t xscore_pci_format_proc_pci(char *buf, size_t max_size) {
    if (!buf || max_size == 0) return 0;
    size_t off = 0;
    off = str_append(buf, max_size, off, "PCI devices found:\n");

    for (uint32_t i = 0; i < device_count; ++i) {
        const struct xscore_pci_device *d = &devices[i];
        off = str_append(buf, max_size, off, "  Bus ");
        off = str_append_u32(buf, max_size, off, d->bus);
        off = str_append(buf, max_size, off, ", device ");
        off = str_append_u32(buf, max_size, off, d->slot);
        off = str_append(buf, max_size, off, ", function ");
        off = str_append_u32(buf, max_size, off, d->function);
        off = str_append(buf, max_size, off, ":\n    Class ");
        off = str_append_hex(buf, max_size, off, ((uint16_t)d->class_code << 8) | d->subclass, 4);
        off = str_append(buf, max_size, off, ": ");
        off = str_append(buf, max_size, off, xscore_pci_class_name(d->class_code, d->subclass));
        off = str_append(buf, max_size, off, " (rev ");
        off = str_append_u32(buf, max_size, off, d->revision);
        off = str_append(buf, max_size, off, ").\n    Vendor 0x");
        off = str_append_hex(buf, max_size, off, d->vendor_id, 4);
        off = str_append(buf, max_size, off, " Device 0x");
        off = str_append_hex(buf, max_size, off, d->device_id, 4);
        off = str_append(buf, max_size, off, " [");
        off = str_append(buf, max_size, off, xscore_pci_device_name(d->vendor_id, d->device_id));
        off = str_append(buf, max_size, off, "]\n");

        if (d->pcie_cap_offset) {
            off = str_append(buf, max_size, off, "    PCIe Gen");
            off = str_append_u32(buf, max_size, off, d->pcie_link_speed);
            off = str_append(buf, max_size, off, " x");
            off = str_append_u32(buf, max_size, off, d->pcie_link_width);
            off = str_append(buf, max_size, off, "\n");
        }

        for (int b = 0; b < 6; ++b) {
            if (d->bars[b]) {
                off = str_append(buf, max_size, off, "      BAR");
                off = str_append_u32(buf, max_size, off, b);
                off = str_append(buf, max_size, off, ": ");
                off = str_append(buf, max_size, off, d->bar_is_io[b] ? "I/O at 0x" : "Memory at 0x");
                off = str_append_hex(buf, max_size, off, d->bars[b], 8);
                if (d->bar_sizes[b]) {
                    off = str_append(buf, max_size, off, " [size=");
                    uint32_t sz = d->bar_sizes[b];
                    if (sz >= 1024 * 1024) {
                        off = str_append_u32(buf, max_size, off, sz / (1024 * 1024));
                        off = str_append(buf, max_size, off, "M");
                    } else if (sz >= 1024) {
                        off = str_append_u32(buf, max_size, off, sz / 1024);
                        off = str_append(buf, max_size, off, "K");
                    } else {
                        off = str_append_u32(buf, max_size, off, sz);
                    }
                    off = str_append(buf, max_size, off, "]");
                }
                off = str_append(buf, max_size, off, "\n");
            }
        }
    }

    return off;
}
