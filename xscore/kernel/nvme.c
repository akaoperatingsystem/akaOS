#include <xscore/nvme.h>
#include <xscore/pci.h>
#include <xscore/serial.h>
#include <xscore/memory.h>
#include <xscore/vmm.h>
#include <xscore/smp.h>
#include <xscore/timer.h>

void *memset(void *dest, int value, size_t count);
void *memcpy(void *dest, const void *src, size_t count);
#include <stddef.h>

#define NVME_REG_CAP       0x0000U
#define NVME_REG_VS        0x0008U
#define NVME_REG_CC        0x0014U
#define NVME_REG_CSTS      0x001cU
#define NVME_REG_AQA       0x0024U
#define NVME_REG_ASQ       0x0028U
#define NVME_REG_ACQ       0x0030U
#define NVME_REG_DOORBELL  0x1000U

#define NVME_CC_EN         (1U << 0)
#define NVME_CC_CSS_NVM    (0U << 4)
#define NVME_CC_MPS_4K     (0U << 7)
#define NVME_CC_AMS_RR     (0U << 11)
#define NVME_CC_SHN_NONE   (0U << 14)
#define NVME_CC_IOSQES     (6U << 16)
#define NVME_CC_IOCQES     (4U << 20)
#define NVME_CSTS_RDY      (1U << 0)
#define NVME_CSTS_CFS      (1U << 1)

#define NVME_ADMIN_QUEUE_SIZE 64U
#define NVME_ADMIN_IDENTIFY 0x06U
#define NVME_ADMIN_CREATE_IO_SQ 0x01U
#define NVME_ADMIN_CREATE_IO_CQ 0x05U
#define NVME_IDENTIFY_NS 0x00U
#define NVME_IDENTIFY_CTRL 0x01U
#define NVME_IDENTIFY_ACTIVE_NS 0x02U
#define NVME_IO_WRITE 0x01U
#define NVME_IO_READ 0x02U
#define NVME_IO_FLUSH 0x00U
#define NVME_PCI_CLASS 0x01U
#define NVME_PCI_SUBCLASS 0x08U
#define NVME_PCI_PROGIF 0x02U
#define NVME_POLL_LIMIT 10000000U
#define NVME_PAGE_SIZE 4096U
#define NVME_MMIO_FLAGS (XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE | UINT64_C(0x18))
#define NVME_IO_QUEUE_ID 1U

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
    volatile uint16_t status;
} __attribute__((packed));

_Static_assert(sizeof(struct nvme_sq_entry) == 64, "NVMe SQ entry must be 64 bytes");
_Static_assert(sizeof(struct nvme_cq_entry) == 16, "NVMe CQ entry must be 16 bytes");

struct nvme_controller {
    uint64_t bar_base;
    uint64_t bar_size;
    uint32_t dstrd;
    uint16_t queue_depth;
    uint16_t admin_sq_tail;
    uint16_t admin_cq_head;
    uint16_t io_sq_tail;
    uint16_t io_cq_head;
    uint16_t command_id;
    uint8_t admin_cq_phase;
    uint8_t io_cq_phase;
    uint8_t admin_ready;
    uint8_t io_ready;
    uint64_t asq_phys;
    uint64_t acq_phys;
    uint64_t iosq_phys;
    uint64_t iocq_phys;
    struct nvme_sq_entry *asq;
    struct nvme_cq_entry *acq;
    struct nvme_sq_entry *iosq;
    struct nvme_cq_entry *iocq;
    struct xscore_nvme_controller_info info;
};

static struct nvme_controller controller;
static uint64_t identify_page_phys;
static uint8_t *identify_page;
static xscore_spinlock_t nvme_io_lock = XSCORE_SPINLOCK_INIT;

static inline uint32_t nvme_mmio_read32(uint32_t reg) {
    return *(volatile uint32_t *)(uintptr_t)(controller.bar_base + reg);
}
static inline void nvme_mmio_write32(uint32_t reg, uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)(controller.bar_base + reg) = value;
}
static inline uint64_t nvme_mmio_read64(uint32_t reg) {
    return *(volatile uint64_t *)(uintptr_t)(controller.bar_base + reg);
}
static inline void nvme_mmio_write64(uint32_t reg, uint64_t value) {
    *(volatile uint64_t *)(uintptr_t)(controller.bar_base + reg) = value;
}

static inline void nvme_cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause");
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield");
#elif defined(__riscv)
    __asm__ volatile("nop");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

static int nvme_poll_timed_out(uint64_t start, uint32_t frequency, uint32_t spin) {
    if (spin >= NVME_POLL_LIMIT) return 1;
    return frequency && xscore_timer_ticks() - start >= (uint64_t)frequency * 10U;
}

static int nvme_wait_ready(uint8_t expected) {
    uint64_t start = xscore_timer_ticks();
    uint32_t hz = xscore_timer_frequency();
    for (uint32_t i = 0; i < NVME_POLL_LIMIT; ++i) {
        uint32_t status = nvme_mmio_read32(NVME_REG_CSTS);
        if (status & NVME_CSTS_CFS) return 0;
        if (((status & NVME_CSTS_RDY) != 0) == (expected != 0)) return 1;
        if (nvme_poll_timed_out(start, hz, i)) break;
        nvme_cpu_relax();
    }
    return 0;
}

static int nvme_map_bar(uint64_t bar, uint64_t size) {
    if (!bar || size < NVME_PAGE_SIZE) return 0;
    uint64_t base = bar & ~(uint64_t)(NVME_PAGE_SIZE - 1U);
    uint64_t end = bar + size;
    if (end < bar || end > UINT64_MAX - (NVME_PAGE_SIZE - 1U)) return 0;
    uint64_t map_end = (end + NVME_PAGE_SIZE - 1U) & ~(uint64_t)(NVME_PAGE_SIZE - 1U);
    if (map_end < end || map_end - base > UINT64_C(0x1000000)) return 0;
    for (uint64_t page = base; page < map_end; page += NVME_PAGE_SIZE) {
        if (xscore_vmm_is_mapped(page, NULL)) continue;
        if (!xscore_vmm_map_page(page, page, NVME_MMIO_FLAGS)) return 0;
    }
    controller.bar_base = bar;
    controller.bar_size = size;
    return 1;
}

static int nvme_wait_completion(struct nvme_cq_entry *cq, uint16_t *head,
                                uint8_t *phase, uint16_t qid,
                                uint16_t expected_cid, uint32_t *result) {
    uint64_t start = xscore_timer_ticks();
    uint32_t hz = xscore_timer_frequency();
    for (uint32_t spin = 0; spin < NVME_POLL_LIMIT; ++spin) {
        uint16_t status = cq[*head].status;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if ((status & 1U) == *phase) {
            uint16_t cid = cq[*head].command_id;
            uint16_t sqid = cq[*head].sq_id;
            uint16_t status_code = (status >> 1) & 0x7ffU;
            uint32_t result_dw0 = cq[*head].cdw0;
            *head = (uint16_t)((*head + 1U) % controller.queue_depth);
            if (*head == 0) *phase ^= 1U;
            uint32_t doorbell = NVME_REG_DOORBELL +
                ((uint32_t)qid * 2U + 1U) * (4U << controller.dstrd);
            nvme_mmio_write32(doorbell, *head);
            if (cid != expected_cid || sqid != qid || status_code != 0) return 0;
            if (result) *result = result_dw0;
            return 1;
        }
        if (nvme_poll_timed_out(start, hz, spin)) break;
        nvme_cpu_relax();
    }
    return 0;
}

static int nvme_submit(struct nvme_sq_entry *sq, struct nvme_cq_entry *cq,
                       uint16_t qid, uint16_t *tail, uint16_t *head,
                       uint8_t *phase, uint8_t opcode, uint32_t nsid,
                       uint64_t prp1, uint64_t prp2,
                       uint32_t cdw10, uint32_t cdw11, uint32_t cdw12,
                       uint32_t *result) {
    if (!sq || !cq) return 0;
    uint16_t cid = ++controller.command_id;
    struct nvme_sq_entry *cmd = &sq[*tail];
    cmd->cdw0 = (uint32_t)opcode | ((uint32_t)cid << 16);
    cmd->nsid = nsid;
    cmd->reserved = 0;
    cmd->mptr = 0;
    cmd->dptr[0] = prp1;
    cmd->dptr[1] = prp2;
    cmd->cdw10 = cdw10;
    cmd->cdw11 = cdw11;
    cmd->cdw12 = cdw12;
    cmd->cdw13 = cmd->cdw14 = cmd->cdw15 = 0;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    *tail = (uint16_t)((*tail + 1U) % controller.queue_depth);
    uint32_t sq_doorbell = NVME_REG_DOORBELL +
        (uint32_t)qid * 2U * (4U << controller.dstrd);
    nvme_mmio_write32(sq_doorbell, *tail);
    return nvme_wait_completion(cq, head, phase, qid, cid, result);
}

static int nvme_admin_command(uint8_t opcode, uint32_t nsid, uint64_t prp1,
                              uint32_t cdw10, uint32_t cdw11, uint32_t *result) {
    if (!controller.admin_ready) return 0;
    return nvme_submit(controller.asq, controller.acq, 0,
                       &controller.admin_sq_tail, &controller.admin_cq_head,
                       &controller.admin_cq_phase, opcode, nsid, prp1, 0,
                       cdw10, cdw11, 0, result);
}

static void copy_trimmed(char *dst, size_t dst_size, const uint8_t *src, size_t src_size) {
    if (!dst || dst_size == 0) return;
    size_t length = src_size < dst_size - 1U ? src_size : dst_size - 1U;
    while (length && (src[length - 1U] == ' ' || src[length - 1U] == 0)) --length;
    for (size_t i = 0; i < length; ++i) dst[i] = (char)src[i];
    dst[length] = '\0';
}

static int nvme_identify_controller(void) {
    identify_page_phys = xscore_pmm_allocate_page();
    if (!identify_page_phys) return 0;
    identify_page = (uint8_t *)(uintptr_t)identify_page_phys;
    memset(identify_page, 0, NVME_PAGE_SIZE);
    if (!nvme_admin_command(NVME_ADMIN_IDENTIFY, 0, identify_page_phys,
                            NVME_IDENTIFY_CTRL, 0, NULL)) return 0;
    copy_trimmed(controller.info.serial, sizeof(controller.info.serial), identify_page + 4, 20);
    copy_trimmed(controller.info.model, sizeof(controller.info.model), identify_page + 24, 40);
    copy_trimmed(controller.info.firmware, sizeof(controller.info.firmware), identify_page + 64, 8);
    uint8_t mdts = identify_page[77];
    uint64_t cap = nvme_mmio_read64(NVME_REG_CAP);
    uint8_t mpsmin = (uint8_t)((cap >> 48) & 0xfU);
    if (mdts == 0 || mdts + 12U + mpsmin >= 32U) controller.info.max_transfer_bytes = UINT32_MAX;
    else controller.info.max_transfer_bytes = 1U << (12U + mpsmin + mdts);
    controller.info.reported_namespace_count = *(const uint32_t *)(const void *)(identify_page + 516);
    controller.info.namespace_count = 0;
    controller.info.version = nvme_mmio_read32(NVME_REG_VS);
    return 1;
}

static int nvme_identify_namespaces(void) {
    uint32_t nsids[XSCORE_NVME_MAX_NAMESPACES];
    for (size_t i = 0; i < NVME_PAGE_SIZE; ++i) identify_page[i] = 0;
    /* CNS=2: active namespace ID list. */
    if (!nvme_admin_command(NVME_ADMIN_IDENTIFY, 0, identify_page_phys, 2U, 0, NULL)) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < XSCORE_NVME_MAX_NAMESPACES; ++i) {
        uint32_t nsid = *(const uint32_t *)(const void *)(identify_page + i * sizeof(uint32_t));
        if (!nsid) break;
        nsids[count++] = nsid;
    }
    for (uint32_t i = 0; i < count; ++i) {
        for (size_t b = 0; b < NVME_PAGE_SIZE; ++b) identify_page[b] = 0;
        if (!nvme_admin_command(NVME_ADMIN_IDENTIFY, nsids[i], identify_page_phys,
                                NVME_IDENTIFY_NS, 0, NULL)) continue;
        uint64_t nsze = *(const uint64_t *)(const void *)(identify_page + 0);
        uint64_t ncap = *(const uint64_t *)(const void *)(identify_page + 8);
        uint8_t nlbaf = identify_page[25];
        uint8_t flbas = identify_page[26] & 0x0fU;
        if (!nsze || !ncap || flbas > nlbaf) continue;
        size_t lbaf_offset = 128U + (size_t)flbas * 4U;
        uint8_t lbads = identify_page[lbaf_offset + 2U];
        /* Current DMA bounce path transfers at most one 4 KiB page per command. */
        if (lbads < 9U || lbads > 12U) continue;
        if (controller.info.namespace_count >= XSCORE_NVME_MAX_NAMESPACES) break;
        struct xscore_nvme_namespace_info *ns = &controller.info.namespaces[controller.info.namespace_count++];
        ns->nsid = nsids[i];
        ns->size_lbas = nsze;
        ns->capacity_lbas = ncap;
        ns->lba_size_bytes = 1U << lbads;
        ns->active = 1;
    }
    return controller.info.namespace_count != 0;
}

static int nvme_create_io_queues(void) {
    controller.iosq_phys = xscore_pmm_allocate_page();
    controller.iocq_phys = xscore_pmm_allocate_page();
    if (!controller.iosq_phys || !controller.iocq_phys) return 0;
    controller.iosq = (struct nvme_sq_entry *)(uintptr_t)controller.iosq_phys;
    controller.iocq = (struct nvme_cq_entry *)(uintptr_t)controller.iocq_phys;
    memset(controller.iosq, 0, NVME_PAGE_SIZE);
    memset(controller.iocq, 0, NVME_PAGE_SIZE);
    controller.io_sq_tail = 0;
    controller.io_cq_head = 0;
    controller.io_cq_phase = 1;
    uint32_t size_minus_one = controller.queue_depth - 1U;

    /* Create I/O Completion Queue 1, physically contiguous, polling mode. */
    if (!nvme_admin_command(NVME_ADMIN_CREATE_IO_CQ, 0, controller.iocq_phys,
                            (size_minus_one << 16) | NVME_IO_QUEUE_ID,
                            1U, NULL)) return 0;
    /* Create I/O Submission Queue 1, associated with CQ 1. */
    if (!nvme_admin_command(NVME_ADMIN_CREATE_IO_SQ, 0, controller.iosq_phys,
                            (size_minus_one << 16) | NVME_IO_QUEUE_ID,
                            (NVME_IO_QUEUE_ID << 16) | 1U, NULL)) return 0;
    controller.io_ready = 1;
    return 1;
}

static const struct xscore_nvme_namespace_info *nvme_find_namespace(uint32_t nsid) {
    for (uint32_t i = 0; i < controller.info.namespace_count; ++i)
        if (controller.info.namespaces[i].active && controller.info.namespaces[i].nsid == nsid)
            return &controller.info.namespaces[i];
    return NULL;
}

static int nvme_transfer(uint32_t nsid, uint64_t lba, uint32_t count,
                         void *buffer, uint8_t opcode) {
    const struct xscore_nvme_namespace_info *ns = nvme_find_namespace(nsid);
    if (!controller.io_ready || !ns || !count || !buffer ||
        lba >= ns->capacity_lbas || count > ns->capacity_lbas - lba) return 0;
    size_t lba_size = ns->lba_size_bytes;
    uint32_t blocks_per_page = (uint32_t)(NVME_PAGE_SIZE / lba_size);
    if (!blocks_per_page) return 0;

    uintptr_t irq_flags = xscore_spin_lock_irqsave(&nvme_io_lock);
    uint64_t bounce_phys = xscore_pmm_allocate_page();
    if (!bounce_phys) {
        xscore_spin_unlock_irqrestore(&nvme_io_lock, irq_flags);
        return 0;
    }
    uint8_t *bounce = (uint8_t *)(uintptr_t)bounce_phys;
    uint8_t *user = (uint8_t *)buffer;
    uint32_t done = 0;
    int ok = 1;
    while (done < count) {
        uint32_t chunk = count - done;
        if (chunk > blocks_per_page) chunk = blocks_per_page;
        size_t bytes = (size_t)chunk * lba_size;
        if (opcode == NVME_IO_WRITE) {
            memcpy(bounce, user + (size_t)done * lba_size, bytes);
            __atomic_thread_fence(__ATOMIC_RELEASE);
        }
        uint64_t current_lba = lba + done;
        uint32_t lba_count = chunk - 1U;
        if (!nvme_submit(controller.iosq, controller.iocq, NVME_IO_QUEUE_ID,
                         &controller.io_sq_tail, &controller.io_cq_head,
                         &controller.io_cq_phase, opcode, nsid, bounce_phys, 0,
                         (uint32_t)current_lba, (uint32_t)(current_lba >> 32),
                         lba_count, NULL)) {
            ok = 0;
            break;
        }
        if (opcode == NVME_IO_READ) {
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            memcpy(user + (size_t)done * lba_size, bounce, bytes);
        }
        done += chunk;
    }
    (void)xscore_pmm_free_page(bounce_phys);
    xscore_spin_unlock_irqrestore(&nvme_io_lock, irq_flags);
    return ok;
}


static int nvme_admin_submit(uint8_t opcode, uint32_t nsid, uint64_t prp1, uint64_t prp2,
                             uint32_t cdw10, uint32_t cdw11, uint32_t cdw12, uint32_t *result) {
    if (!controller.admin_ready) return 0;
    
    // We need to use the admin queue
    // I should create a helper that locks if necessary (though admin is usually synchronous)
    return nvme_submit(controller.asq, controller.acq, 0,
                       &controller.admin_sq_tail, &controller.admin_cq_head,
                       &controller.admin_cq_phase, opcode, nsid, prp1, prp2,
                       cdw10, cdw11, cdw12, result);
}

int xscore_nvme_initialize(void) {
    memset(&controller, 0, sizeof(controller));
    uint32_t devices = xscore_pci_device_count();
    for (uint32_t i = 0; i < devices; ++i) {
        const struct xscore_pci_device *dev = xscore_pci_get_device(i);
        if (!dev || dev->class_code != 0x01 || dev->subclass != 0x08 || dev->prog_if != 0x02) continue;
        xscore_serial_write("[NVMe] PCIe NVM Express controller discovered.\n");
        if (!xscore_pci_enable_bus_master(dev)) {
            xscore_serial_write("[NVMe] PCI memory/bus mastering enable failed.\n");
            continue;
        }
        if (dev->bar_is_io[0] || !dev->bars[0] || dev->bar_sizes[0] < NVME_PAGE_SIZE) {
            xscore_serial_write("[NVMe] Invalid BAR0 memory aperture.\n");
            continue;
        }
        if (!nvme_map_bar(dev->bars[0], dev->bar_sizes[0])) {
            xscore_serial_write("[NVMe] BAR0 MMIO mapping failed.\n");
            continue;
        }

        uint64_t cap = nvme_mmio_read64(NVME_REG_CAP);
        uint16_t mqes = (uint16_t)(cap & 0xffffU);
        uint8_t mpsmin = (uint8_t)((cap >> 48) & 0xfU);
        uint8_t mpsmax = (uint8_t)((cap >> 52) & 0xfU);
        uint8_t css = (uint8_t)((cap >> 37) & 0xffU);
        controller.dstrd = (uint32_t)((cap >> 32) & 0xfU);
        uint32_t max_depth = (uint32_t)mqes + 1U;
        uint32_t stride = 4U << controller.dstrd;
        uint64_t max_doorbell = NVME_REG_DOORBELL + (uint64_t)3U * stride + sizeof(uint32_t);
        if (mpsmin != 0 || mpsmax < mpsmin || (css & 1U) == 0 || mqes < 1U ||
            max_doorbell > controller.bar_size) {
            xscore_serial_write("[NVMe] Unsupported CAP geometry or 4K memory page capability.\n");
            continue;
        }
        controller.queue_depth = (max_depth < NVME_ADMIN_QUEUE_SIZE)
                                     ? (uint16_t)max_depth : NVME_ADMIN_QUEUE_SIZE;
        controller.info.max_queue_entries = (uint16_t)max_depth;
        controller.info.doorbell_stride = (uint8_t)controller.dstrd;

        uint32_t cc = nvme_mmio_read32(NVME_REG_CC);
        if (cc & NVME_CC_EN) nvme_mmio_write32(NVME_REG_CC, cc & ~NVME_CC_EN);
        if (!nvme_wait_ready(0)) {
            xscore_serial_write("[NVMe] Controller did not become disabled.\n");
            continue;
        }

        controller.asq_phys = xscore_pmm_allocate_page();
        controller.acq_phys = xscore_pmm_allocate_page();
        if (!controller.asq_phys || !controller.acq_phys) {
            xscore_serial_write("[NVMe] Admin queue page allocation failed.\n");
            continue;
        }
        controller.asq = (struct nvme_sq_entry *)(uintptr_t)controller.asq_phys;
        controller.acq = (struct nvme_cq_entry *)(uintptr_t)controller.acq_phys;
        memset(controller.asq, 0, NVME_PAGE_SIZE);
        memset((void *)controller.acq, 0, NVME_PAGE_SIZE);
        controller.admin_sq_tail = 0;
        controller.admin_cq_head = 0;
        controller.command_id = 0;
        controller.admin_cq_phase = 1;

        uint32_t size_minus_one = controller.queue_depth - 1U;
        nvme_mmio_write32(NVME_REG_AQA, (size_minus_one << 16) | size_minus_one);
        nvme_mmio_write64(NVME_REG_ASQ, controller.asq_phys);
        nvme_mmio_write64(NVME_REG_ACQ, controller.acq_phys);
        uint32_t cc_value = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
                            NVME_CC_AMS_RR | NVME_CC_SHN_NONE | NVME_CC_IOSQES | NVME_CC_IOCQES;
        nvme_mmio_write32(NVME_REG_CC, cc_value);
        if (!nvme_wait_ready(1)) {
            xscore_serial_write("[NVMe] Controller failed to become ready.\n");
            continue;
        }
        controller.admin_ready = 1;

        if (!nvme_identify_controller()) {
            xscore_serial_write("[NVMe] Identify Controller command failed.\n");
            controller.admin_ready = 0;
            continue;
        }
        (void)nvme_identify_namespaces();
        if (controller.info.namespace_count == 0) {
            xscore_serial_write("[NVMe] Controller identified but no active namespaces reported.\n");
            controller.admin_ready = 0;
            continue;
        }
        if (!nvme_create_io_queues()) {
            xscore_serial_write("[NVMe] Create I/O queue commands failed.\n");
            controller.admin_ready = 0;
            continue;
        }

        if (identify_page_phys) {
            (void)xscore_pmm_free_page(identify_page_phys);
            identify_page_phys = 0;
            identify_page = NULL;
        }
        controller.info.ready = 1;
        xscore_serial_write("[NVMe] Admin & I/O queues ready; controller and active namespaces identified.\n");
        return 1;
    }
    return 0;
}

int xscore_nvme_present(void) {
    return controller.info.ready && controller.io_ready && controller.info.namespace_count != 0;
}

const struct xscore_nvme_controller_info *xscore_nvme_get_info(void) {
    return controller.info.ready ? &controller.info : NULL;
}

int xscore_nvme_read_blocks(uint32_t nsid, uint64_t lba, uint32_t count, void *buffer) {
    return nvme_transfer(nsid, lba, count, buffer, NVME_IO_READ);
}

int xscore_nvme_write_blocks(uint32_t nsid, uint64_t lba, uint32_t count, const void *buffer) {
    return nvme_transfer(nsid, lba, count, (void *)buffer, NVME_IO_WRITE);
}

int xscore_nvme_flush(uint32_t nsid) {
    if (!controller.io_ready || !nvme_find_namespace(nsid)) return 0;
    uintptr_t flags = xscore_spin_lock_irqsave(&nvme_io_lock);
    int ok = nvme_submit(controller.iosq, controller.iocq, NVME_IO_QUEUE_ID,
                         &controller.io_sq_tail, &controller.io_cq_head,
                         &controller.io_cq_phase, NVME_IO_FLUSH, nsid, 0, 0,
                         0, 0, 0, NULL);
    xscore_spin_unlock_irqrestore(&nvme_io_lock, flags);
    return ok;
}

int xscore_nvme_block_present(void) {
    return xscore_nvme_present() && controller.info.namespaces[0].lba_size_bytes == 512U;
}

uint64_t xscore_nvme_sector_count(void) {
    if (!xscore_nvme_block_present()) return 0;
    return controller.info.namespaces[0].capacity_lbas;
}

int xscore_nvme_read_sector(uint64_t sector, void *buffer) {
    if (!xscore_nvme_block_present()) return 0;
    return xscore_nvme_read_blocks(controller.info.namespaces[0].nsid, sector, 1, buffer);
}

int xscore_nvme_write_sector(uint64_t sector, const void *buffer) {
    if (!xscore_nvme_block_present()) return 0;
    int ok = xscore_nvme_write_blocks(controller.info.namespaces[0].nsid, sector, 1, buffer);
    if (ok) ok = xscore_nvme_flush(controller.info.namespaces[0].nsid);
    return ok;
}


int xscore_nvme_get_smart_log(uint32_t nsid, struct nvme_smart_log *log) {
    if (!log) return 0;
    
    // Allocate a page for the log data (NVMe log pages are usually 512 bytes, but need 4KB alignment)
    uint64_t log_phys = xscore_pmm_allocate_page();
    if (!log_phys) return 0;
    
    // Command: Get Log Page (0x02)
    // CDW10: LID (0x02 for SMART), Number of DWords (0x02 is 512 bytes -> (512/4)-1 = 127)
    // Actually SMART log is 512 bytes, so 128 DWords. (512/4) - 1 = 127
    uint32_t cdw10 = 0x02 | (127 << 16); 
    
    // PRP1: Physical address of buffer
    uint32_t result = 0;
    if (!nvme_admin_submit(0x02, nsid, log_phys, 0, cdw10, 0, 0, &result)) {
        xscore_pmm_free_page(log_phys);
        return 0;
    }
    
    memcpy(log, (void *)(uintptr_t)log_phys, sizeof(struct nvme_smart_log));
    xscore_pmm_free_page(log_phys);
    return 1;
}

