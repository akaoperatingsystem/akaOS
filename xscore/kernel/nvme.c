#include <xscore/nvme.h>
#include <xscore/pci.h>
#include <xscore/serial.h>
#include <xscore/memory.h>
#include <xscore/vmm.h>
#include <shim/linux_nvme.h>
#include <shim/linux_nvme_host.h>

#define NVME_REG_CAP   0x0000
#define NVME_REG_VS    0x0008
#define NVME_REG_CC    0x0014
#define NVME_REG_CSTS  0x001c
#define NVME_REG_AQA   0x0024
#define NVME_REG_ASQ   0x0028
#define NVME_REG_ACQ   0x0030

#define NVME_CC_ENABLE     (1 << 0)
#define NVME_CC_CSS_NVM    (0 << 4)
#define NVME_CC_MPS_4K     (0 << 7)
#define NVME_CC_AMS_RR     (0 << 11)
#define NVME_CC_SHN_NONE   (0 << 14)
#define NVME_CC_IOSQES     (6 << 16)
#define NVME_CC_IOCQES     (4 << 20)

#define NVME_CSTS_RDY      (1 << 0)
#define NVME_CSTS_CFS      (1 << 1)

#define NVME_ADMIN_QUEUE_SIZE 64
#define NVME_IO_QUEUE_SIZE    64

struct nvme_sq_entry {
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t mptr;
    uint64_t dptr[2];
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed));

struct nvme_cq_entry {
    uint32_t cdw0;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
} __attribute__((packed));

struct xscore_nvme_controller {
    uintptr_t bar_vaddr;
    uint64_t bar_paddr;
    uint64_t bar_size;
    uint32_t doorbell_stride;
    
    // Admin queue
    uint64_t asq_paddr;
    uint64_t acq_paddr;
    struct nvme_sq_entry *asq;
    struct nvme_cq_entry *acq;
    uint16_t asq_tail;
    uint16_t acq_head;
    uint8_t acq_phase;
    
    // Controller status
    int present;
    uint32_t ns_count;
    uint64_t ns_size_sectors;
};

static struct xscore_nvme_controller g_nvme;

static inline uint32_t nvme_read32(struct xscore_nvme_controller *ctrl, uint32_t reg) {
    return *(volatile uint32_t *)(ctrl->bar_vaddr + reg);
}

static inline void nvme_write32(struct xscore_nvme_controller *ctrl, uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(ctrl->bar_vaddr + reg) = val;
}

static inline uint64_t nvme_read64(struct xscore_nvme_controller *ctrl, uint32_t reg) {
    return *(volatile uint64_t *)(ctrl->bar_vaddr + reg);
}

static inline void nvme_write64(struct xscore_nvme_controller *ctrl, uint32_t reg, uint64_t val) {
    *(volatile uint64_t *)(ctrl->bar_vaddr + reg) = val;
}

static void spin_delay_ms(uint32_t ms) {
    for (uint64_t i = 0; i < (uint64_t)ms * 100000; i++) {
        __asm__ volatile("pause");
    }
}

static int nvme_wait_ready(struct xscore_nvme_controller *ctrl, int expected_ready) {
    for (int i = 0; i < 5000; i++) {
        uint32_t csts = nvme_read32(ctrl, NVME_REG_CSTS);
        if (csts & NVME_CSTS_CFS) {
            xscore_serial_write("[ERROR] NVMe controller fatal status!\n");
            return 0;
        }
        int rdy = (csts & NVME_CSTS_RDY) != 0;
        if (rdy == expected_ready) return 1;
        spin_delay_ms(1);
    }
    return 0;
}

int xscore_nvme_initialize(void) {
    uint32_t device_count = xscore_pci_device_count();
    for (uint32_t i = 0; i < device_count; i++) {
        const struct xscore_pci_device *pci_dev = xscore_pci_get_device(i);
        if (pci_dev->class_code == 0x01 && pci_dev->subclass == 0x08) {
            xscore_serial_write("[INFO] NVMe PCIe storage controller detected! Initializing zero-copy NVMe driver...\n");

            if (pci_dev->bar_is_io[0] || !pci_dev->bars[0]) {
                xscore_serial_write("[ERROR] NVMe invalid BAR0\n");
                continue;
            }

            g_nvme.bar_paddr = pci_dev->bars[0];
            g_nvme.bar_size = pci_dev->bar_sizes[0];
            g_nvme.bar_vaddr = (uintptr_t)g_nvme.bar_paddr;

            // Map MMIO BAR in VMM
            uint64_t map_size = (g_nvme.bar_size + 4095) & ~4095ULL;
            if (map_size < 16384) map_size = 16384;
            for (uint64_t offset = 0; offset < map_size; offset += 4096) {
                uint64_t paddr = g_nvme.bar_paddr + offset;
                xscore_vmm_map_page(paddr, paddr, XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE);
            }

            uint64_t cap = nvme_read64(&g_nvme, NVME_REG_CAP);
            g_nvme.doorbell_stride = 4 << ((cap >> 32) & 0xf);

            // Disable controller first
            uint32_t cc = nvme_read32(&g_nvme, NVME_REG_CC);
            if (cc & NVME_CC_ENABLE) {
                nvme_write32(&g_nvme, NVME_REG_CC, cc & ~NVME_CC_ENABLE);
            }
            if (!nvme_wait_ready(&g_nvme, 0)) {
                xscore_serial_write("[ERROR] Failed to disable NVMe controller\n");
                continue;
            }

            // Allocate 4K page for Admin Submission Queue
            g_nvme.asq_paddr = xscore_pmm_allocate_page();
            if (!g_nvme.asq_paddr) {
                xscore_serial_write("[ERROR] NVMe failed to allocate ASQ page\n");
                continue;
            }
            g_nvme.asq = (struct nvme_sq_entry *)(uintptr_t)g_nvme.asq_paddr;
            for (size_t j = 0; j < 4096; j++) ((uint8_t *)g_nvme.asq)[j] = 0;

            // Allocate 4K page for Admin Completion Queue
            g_nvme.acq_paddr = xscore_pmm_allocate_page();
            if (!g_nvme.acq_paddr) {
                xscore_serial_write("[ERROR] NVMe failed to allocate ACQ page\n");
                continue;
            }
            g_nvme.acq = (struct nvme_cq_entry *)(uintptr_t)g_nvme.acq_paddr;
            for (size_t j = 0; j < 4096; j++) ((uint8_t *)g_nvme.acq)[j] = 0;

            g_nvme.asq_tail = 0;
            g_nvme.acq_head = 0;
            g_nvme.acq_phase = 1;

            // Setup Admin Queue Attributes (AQA)
            uint32_t aqa = ((NVME_ADMIN_QUEUE_SIZE - 1) << 16) | (NVME_ADMIN_QUEUE_SIZE - 1);
            nvme_write32(&g_nvme, NVME_REG_AQA, aqa);
            nvme_write64(&g_nvme, NVME_REG_ASQ, g_nvme.asq_paddr);
            nvme_write64(&g_nvme, NVME_REG_ACQ, g_nvme.acq_paddr);

            // Enable controller
            uint32_t target_cc = NVME_CC_ENABLE | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
                                 NVME_CC_AMS_RR | NVME_CC_SHN_NONE | NVME_CC_IOSQES | NVME_CC_IOCQES;
            nvme_write32(&g_nvme, NVME_REG_CC, target_cc);

            if (!nvme_wait_ready(&g_nvme, 1)) {
                xscore_serial_write("[ERROR] NVMe controller failed to ready\n");
                continue;
            }

            g_nvme.present = 1;
            xscore_serial_write("[INFO] NVMe controller ready and Admin Queues online (lockless zero-copy ring)\n");
            return 1;
        }
    }
    return 0;
}

int xscore_nvme_present(void) {
    return g_nvme.present;
}
