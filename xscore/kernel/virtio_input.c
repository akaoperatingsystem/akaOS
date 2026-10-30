#include <xscore/virtio_input.h>
#include <xscore/tty.h>
#include <xscore/vmm.h>
#include <xscore/serial.h>
#include <xscore/input.h>
#include <stddef.h>
#include <stdint.h>

#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_VENDOR_ID           0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_ALIGN         0x03c
#define VIRTIO_MMIO_QUEUE_PFN           0x040
#define VIRTIO_MMIO_QUEUE_READY         0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0a4

#define VIRTIO_ID_INPUT 18

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4

#define VIRTQ_DESC_F_WRITE 2
#define QUEUE_SIZE 32

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QUEUE_SIZE];
    uint16_t used_event;
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[QUEUE_SIZE];
    uint16_t avail_event;
};

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};

#define QUEUE_MEM_SIZE 8192
#define INPUT_DEVICE_MAX 4

struct virtio_input_device {
    uint64_t mmio;
    uint32_t version;
    uint16_t last_used_idx;
    uint8_t queue_mem[QUEUE_MEM_SIZE] __attribute__((aligned(4096)));
    volatile struct virtio_input_event events[QUEUE_SIZE]
        __attribute__((aligned(8)));
};

static struct virtio_input_device g_input_devices[INPUT_DEVICE_MAX];
static uint32_t g_input_device_count;

static volatile struct virtq_desc *device_desc(
    struct virtio_input_device *device) {
    return (volatile struct virtq_desc *)(device->queue_mem + 0);
}

static volatile struct virtq_avail *device_avail(
    struct virtio_input_device *device) {
    return (volatile struct virtq_avail *)(
        device->queue_mem + sizeof(struct virtq_desc) * QUEUE_SIZE);
}

static volatile struct virtq_used *device_used(
    struct virtio_input_device *device) {
    return (volatile struct virtq_used *)(device->queue_mem + 4096);
}

static inline uint32_t mmio_read32(uint64_t base, uint32_t offset) {
    return *(volatile uint32_t *)(uintptr_t)(base + offset);
}

static inline void mmio_write32(uint64_t base, uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)(base + offset) = val;
}

static int try_init_mmio(uint64_t base) {
    if (g_input_device_count >= INPUT_DEVICE_MAX) return 0;
    /* Map MMIO page */
    xscore_vmm_map_page(base, base, XSCORE_PAGE_WRITABLE);

    uint32_t magic = mmio_read32(base, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != 0x74726976) return 0; /* 'virt' */

    uint32_t dev_id = mmio_read32(base, VIRTIO_MMIO_DEVICE_ID);
    if (dev_id != VIRTIO_ID_INPUT) return 0;

    uint32_t version = mmio_read32(base, VIRTIO_MMIO_VERSION);
    struct virtio_input_device *device =
        &g_input_devices[g_input_device_count];
    device->version = version;
    device->mmio = base;

    /* Reset device */
    mmio_write32(base, VIRTIO_MMIO_STATUS, 0);

    /* Acknowledge and Driver */
    mmio_write32(base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    if (version >= 2) {
        /* Negotiate features: VIRTIO_F_VERSION_1 (bit 32 -> word 1 bit 0) */
        mmio_write32(base, 0x014 /* DEVICE_FEATURES_SEL */, 1);
        uint32_t f1 = mmio_read32(base, 0x010 /* DEVICE_FEATURES */);
        mmio_write32(base, 0x024 /* DRIVER_FEATURES_SEL */, 1);
        mmio_write32(base, 0x020 /* DRIVER_FEATURES */, f1 & 1);

        mmio_write32(base, 0x014, 0);
        uint32_t f0 = mmio_read32(base, 0x010);
        mmio_write32(base, 0x024, 0);
        mmio_write32(base, 0x020, f0);

        /* Set FEATURES_OK (8) */
        mmio_write32(base, VIRTIO_MMIO_STATUS, 1 | 2 | 8);
        uint32_t status = mmio_read32(base, VIRTIO_MMIO_STATUS);
        if (!(status & 8)) {
            xscore_serial_write("[VIRTIO] FEATURES_OK not accepted!\n");
            return 0;
        }
    }

    /* Select queue 0 (eventq) */
    mmio_write32(base, VIRTIO_MMIO_QUEUE_SEL, 0);
    uint32_t max_num = mmio_read32(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max_num < QUEUE_SIZE) {
        xscore_serial_write("[VIRTIO] Queue 0 max_num too small\n");
        return 0;
    }

    mmio_write32(base, VIRTIO_MMIO_QUEUE_NUM, QUEUE_SIZE);

    /* Zero queue memory */
    for (size_t k = 0; k < QUEUE_MEM_SIZE; ++k) {
        device->queue_mem[k] = 0;
    }

    /* Initialize descriptor table and buffers */
    volatile struct virtq_desc *desc = device_desc(device);
    volatile struct virtq_avail *avail = device_avail(device);
    volatile struct virtq_used *used = device_used(device);

    for (int i = 0; i < QUEUE_SIZE; ++i) {
        desc[i].addr = (uint64_t)(uintptr_t)&device->events[i];
        desc[i].len = sizeof(struct virtio_input_event);
        desc[i].flags = VIRTQ_DESC_F_WRITE;
        desc[i].next = 0;
        avail->ring[i] = (uint16_t)i;
    }
    avail->flags = 0;
    avail->idx = QUEUE_SIZE;
    used->flags = 0;
    used->idx = 0;
    device->last_used_idx = 0;

    if (version == 1) {
        /* Legacy VirtIO MMIO */
        mmio_write32(base, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        uint64_t pfn = (uint64_t)(uintptr_t)device->queue_mem / 4096;
        mmio_write32(base, VIRTIO_MMIO_QUEUE_PFN, (uint32_t)pfn);
    } else {
        /* Modern VirtIO MMIO (Version 2) */
        mmio_write32(base, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)(uintptr_t)desc);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)(uintptr_t)desc >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)(uintptr_t)avail);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)((uint64_t)(uintptr_t)avail >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)(uintptr_t)used);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)((uint64_t)(uintptr_t)used >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_READY, 1);
    }

    /* Driver OK */
    mmio_write32(base, VIRTIO_MMIO_STATUS,
                 version >= 2 ? (1 | 2 | 8 | 4) : (1 | 2 | 4));

    /* Notify device that buffers are available */
    mmio_write32(base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    ++g_input_device_count;
    xscore_serial_write("[VIRTIO] VirtIO input event device initialized\n");
    return 1;
}

void xscore_virtio_input_init(void) {
    g_input_device_count = 0;
#if defined(__aarch64__) || defined(__arm__)
    /* Probe ARM virt MMIO slots: 0x0a000000 .. 0x0a003e00 */
    for (uint64_t addr = 0x0a000000; addr < 0x0a004000; addr += 0x200) {
        (void)try_init_mmio(addr);
    }
#elif defined(__riscv)
    /* Probe RISC-V virt MMIO slots: 0x10001000 .. 0x10008000 */
    for (uint64_t addr = 0x10001000; addr <= 0x10008000; addr += 0x1000) {
        (void)try_init_mmio(addr);
    }
#endif
}

void xscore_virtio_input_poll(void) {
    for (uint32_t device_index = 0;
         device_index < g_input_device_count; ++device_index) {
        struct virtio_input_device *device =
            &g_input_devices[device_index];
        volatile struct virtq_used *used = device_used(device);
        volatile struct virtq_avail *avail = device_avail(device);
        uint16_t used_idx = used->idx;
        int refilled = 0;

        while (device->last_used_idx != used_idx) {
            uint16_t slot = device->last_used_idx % QUEUE_SIZE;
            uint32_t elem_id = used->ring[slot].id;
            if (elem_id < QUEUE_SIZE) {
                uint16_t ev_type = device->events[elem_id].type;
                uint16_t ev_code = device->events[elem_id].code;
                uint32_t ev_val = device->events[elem_id].value;

                if (ev_type == AKAOS_EV_KEY) {
                    /* VirtIO pointer buttons share EV_KEY with keyboard keys,
                     * but their codes are outside the keyboard scan range.
                     * Publishing them as scan codes turned BTN_LEFT (272)
                     * into key 16 and made graphical mouse clicks impossible. */
                    if (ev_code >= AKAOS_BTN_LEFT) {
                        xscore_input_publish(AKAOS_EV_KEY, ev_code,
                                             (int32_t)ev_val);
                    } else if (ev_val == 1 || ev_val == 2) {
                        xscore_tty_input_scancode((uint8_t)(ev_code & 0x7f));
                    } else if (ev_val == 0) {
                        xscore_tty_input_scancode(
                            (uint8_t)((ev_code & 0x7f) | 0x80));
                    }
                } else {
                    xscore_input_publish(ev_type, ev_code, (int32_t)ev_val);
                }

                avail->ring[avail->idx % QUEUE_SIZE] = (uint16_t)elem_id;
                avail->idx++;
                refilled = 1;
            }
            device->last_used_idx++;
        }

        if (refilled) {
            mmio_write32(device->mmio, VIRTIO_MMIO_QUEUE_NOTIFY, 0);
        }
    }
}
