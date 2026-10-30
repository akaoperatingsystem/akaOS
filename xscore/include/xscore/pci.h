#ifndef XSCORE_PCI_H
#define XSCORE_PCI_H

#include <stdint.h>
#include <stddef.h>

#define XSCORE_PCI_MAX_DEVICES 64U

struct xscore_pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t header_type;
    uint8_t revision;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t bars[6];
    uint32_t bar_sizes[6];
    uint8_t bar_is_io[6];
    uint8_t pcie_cap_offset;
    uint8_t pcie_link_speed;  /* 1=Gen1 (2.5GT/s), 2=Gen2 (5GT/s), 3=Gen3 (8GT/s), 4=Gen4 (16GT/s), 5=Gen5 (32GT/s) */
    uint8_t pcie_link_width;  /* 1=x1, 2=x2, 4=x4, 8=x8, 16=x16 */
    uint8_t msi_cap_offset;
    uint8_t msix_cap_offset;
};

/* Scan PCI configuration space through standard mechanism #1 ports / ECAM. */
uint32_t xscore_pci_initialize(void);
uint32_t xscore_pci_device_count(void);
const struct xscore_pci_device *xscore_pci_get_device(uint32_t index);
const struct xscore_pci_device *xscore_pci_find(uint16_t vendor_id,
                                                uint16_t device_id);
const struct xscore_pci_device *xscore_pci_find_class(uint8_t class_code,
                                                      uint8_t subclass);
int xscore_pci_enable_bus_master(const struct xscore_pci_device *device);
int xscore_pci_enable_msix(const struct xscore_pci_device *device, uint8_t vector);
uint32_t xscore_pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void xscore_pci_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);
const char *xscore_pci_vendor_name(uint16_t vendor_id);
const char *xscore_pci_device_name(uint16_t vendor_id, uint16_t device_id);
const char *xscore_pci_class_name(uint8_t class_code, uint8_t subclass);
size_t xscore_pci_format_proc_pci(char *buf, size_t max_size);

#endif
