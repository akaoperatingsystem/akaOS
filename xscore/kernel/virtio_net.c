#include <xscore/net.h>
#include <xscore/serial.h>
#include <xscore/vmm.h>
#include <xscore/smp.h>
#include <xscore/timer.h>
#if !defined(__x86_64__) && !defined(__i386__)
#include <xscore/device_irq.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <xscore/pci.h>
#endif
#include <stddef.h>
#include <stdint.h>

#define VIRTIO_VENDOR_ID UINT16_C(0x1af4)
#define VIRTIO_NET_DEVICE_ID UINT16_C(0x1000)

/* Legacy PCI registers */
#define VIRTIO_PCI_HOST_FEATURES  UINT16_C(0x00)
#define VIRTIO_PCI_GUEST_FEATURES UINT16_C(0x04)
#define VIRTIO_PCI_QUEUE_PFN      UINT16_C(0x08)
#define VIRTIO_PCI_QUEUE_NUM      UINT16_C(0x0c)
#define VIRTIO_PCI_QUEUE_SEL      UINT16_C(0x0e)
#define VIRTIO_PCI_QUEUE_NOTIFY   UINT16_C(0x10)
#define VIRTIO_PCI_STATUS         UINT16_C(0x12)
#define VIRTIO_PCI_ISR            UINT16_C(0x13)
#define VIRTIO_PCI_CONFIG         UINT16_C(0x14)

/* MMIO registers */
#define VIRTIO_MMIO_MAGIC_VALUE         UINT32_C(0x000)
#define VIRTIO_MMIO_VERSION             UINT32_C(0x004)
#define VIRTIO_MMIO_DEVICE_ID           UINT32_C(0x008)
#define VIRTIO_MMIO_DEVICE_FEATURES     UINT32_C(0x010)
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL UINT32_C(0x014)
#define VIRTIO_MMIO_DRIVER_FEATURES     UINT32_C(0x020)
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL UINT32_C(0x024)
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     UINT32_C(0x028)
#define VIRTIO_MMIO_QUEUE_SEL           UINT32_C(0x030)
#define VIRTIO_MMIO_QUEUE_NUM_MAX       UINT32_C(0x034)
#define VIRTIO_MMIO_QUEUE_NUM           UINT32_C(0x038)
#define VIRTIO_MMIO_QUEUE_ALIGN         UINT32_C(0x03c)
#define VIRTIO_MMIO_QUEUE_PFN           UINT32_C(0x040)
#define VIRTIO_MMIO_QUEUE_READY         UINT32_C(0x044)
#define VIRTIO_MMIO_QUEUE_NOTIFY        UINT32_C(0x050)
#define VIRTIO_MMIO_INTERRUPT_STATUS    UINT32_C(0x060)
#define VIRTIO_MMIO_INTERRUPT_ACK       UINT32_C(0x064)
#define VIRTIO_MMIO_STATUS              UINT32_C(0x070)
#define VIRTIO_MMIO_QUEUE_DESC_LOW      UINT32_C(0x080)
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     UINT32_C(0x084)
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     UINT32_C(0x090)
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    UINT32_C(0x094)
#define VIRTIO_MMIO_QUEUE_USED_LOW      UINT32_C(0x0a0)
#define VIRTIO_MMIO_QUEUE_USED_HIGH     UINT32_C(0x0a4)
#define VIRTIO_MMIO_CONFIG              UINT32_C(0x100)

/* Status flags */
#define VIRTIO_STATUS_ACKNOWLEDGE UINT8_C(0x01)
#define VIRTIO_STATUS_DRIVER     UINT8_C(0x02)
#define VIRTIO_STATUS_DRIVER_OK  UINT8_C(0x04)
#define VIRTIO_STATUS_FEATURES_OK UINT8_C(0x08)
#define VIRTIO_STATUS_FAILED     UINT8_C(0x80)

#define VIRTQ_DESC_F_NEXT  UINT16_C(1)
#define VIRTQ_DESC_F_WRITE UINT16_C(2)

#define RX_BUFFER_COUNT 16
#define RX_BUFFER_SIZE  (sizeof(struct virtio_net_hdr) + 1536)
#define TX_BUFFER_SIZE  (sizeof(struct virtio_net_hdr) + 1536)
#define QUEUE_MEM_SIZE  16384

struct virtq_desc {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
};

struct virtq_used_elem {
    uint32_t id;
    uint32_t length;
};

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];
};

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[256];
};

struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

enum virtio_transport {
    VIRTIO_TRANSPORT_NONE = 0,
    VIRTIO_TRANSPORT_PCI,
    VIRTIO_TRANSPORT_MMIO
};

static enum virtio_transport transport = VIRTIO_TRANSPORT_NONE;
static uint16_t pci_io_base = 0;
static uint64_t mmio_base = 0;
static int completion_irq;
static uint32_t rx_pending = 1;

void xscore_virtio_net_interrupt(void) {
    __atomic_store_n(&rx_pending, 1, __ATOMIC_RELEASE);
}

static uint8_t rx_queue_mem[QUEUE_MEM_SIZE] __attribute__((aligned(4096)));
static uint8_t tx_queue_mem[QUEUE_MEM_SIZE] __attribute__((aligned(4096)));

static uint8_t rx_buffers[RX_BUFFER_COUNT][RX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buffer[TX_BUFFER_SIZE] __attribute__((aligned(16)));

static uint16_t rx_queue_size = 0;
static uint16_t tx_queue_size = 0;
static uint16_t rx_last_used_idx = 0;
static uint16_t tx_last_used_idx = 0;
/* The legacy/MMIO implementation uses one TX descriptor and one DMA buffer.
 * Serialize callers from AP-owned threads so a concurrent send cannot
 * overwrite the in-flight frame or advance the shared avail/used indices. */
static xscore_spinlock_t virtio_net_tx_lock = XSCORE_SPINLOCK_INIT;

static uint8_t net_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

#if defined(__x86_64__) || defined(__i386__)
static inline void port_out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline void port_out16(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
static inline void port_out32(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t port_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline uint16_t port_in16(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline uint32_t port_in32(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
#endif

#if defined(__aarch64__) || defined(__arm__) || defined(__riscv)
static inline uint32_t mmio_read32(uint64_t base, uint32_t offset) {
    return *(volatile uint32_t *)(uintptr_t)(base + offset);
}

static inline void mmio_write32(uint64_t base, uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)(base + offset) = val;
}
#endif

static inline void memory_barrier(void) {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("dmb sy" : : : "memory");
#elif defined(__riscv)
    __asm__ volatile("fence rw, rw" : : : "memory");
#else
    __asm__ volatile("" : : : "memory");
#endif
}

__attribute__((unused)) static void serial_hex(uint64_t val) {
    char buf[17];
    for (int i = 15; i >= 0; --i) {
        uint8_t n = val & 0xf;
        buf[i] = n < 10 ? '0' + n : 'a' + n - 10;
        val >>= 4;
    }
    buf[16] = 0;
    xscore_serial_write(buf);
}

static inline uint32_t virtq_used_offset(uint16_t qsize) {
    uint32_t avail_end = (uint32_t)sizeof(struct virtq_desc) * qsize +
                         (uint32_t)sizeof(uint16_t) * (2 + qsize);
    return (avail_end + 4095) & ~4095;
}

static void virtio_net_notify(uint16_t queue_index) {
    if (transport == VIRTIO_TRANSPORT_PCI) {
#if defined(__x86_64__) || defined(__i386__)
        port_out16((uint16_t)(pci_io_base + VIRTIO_PCI_QUEUE_NOTIFY), queue_index);
        (void)port_in8((uint16_t)(pci_io_base + VIRTIO_PCI_ISR));
#endif
    } else if (transport == VIRTIO_TRANSPORT_MMIO) {
#if defined(__aarch64__) || defined(__arm__) || defined(__riscv)
        mmio_write32(mmio_base, VIRTIO_MMIO_QUEUE_NOTIFY, queue_index);
#else
        (void)queue_index;
#endif
    }
}

static void setup_initial_rx(void) {
    volatile struct virtq_desc *rx_desc = (volatile struct virtq_desc *)rx_queue_mem;
    volatile struct virtq_avail *rx_avail = (volatile struct virtq_avail *)(
        rx_queue_mem + sizeof(struct virtq_desc) * rx_queue_size);
    uint32_t used_off = virtq_used_offset(rx_queue_size);
    volatile struct virtq_used *rx_used = (volatile struct virtq_used *)(
        rx_queue_mem + used_off);

    rx_last_used_idx = 0;
    rx_avail->flags = 0;
    rx_used->flags = 0;
    rx_used->idx = 0;

    for (uint16_t i = 0; i < RX_BUFFER_COUNT && i < rx_queue_size; ++i) {
        rx_desc[i].address = (uint64_t)(uintptr_t)rx_buffers[i];
        rx_desc[i].length = sizeof(rx_buffers[i]);
        rx_desc[i].flags = VIRTQ_DESC_F_WRITE;
        rx_desc[i].next = 0;
        rx_avail->ring[i] = i;
    }
    memory_barrier();
    rx_avail->idx = RX_BUFFER_COUNT < rx_queue_size ? RX_BUFFER_COUNT : rx_queue_size;
    memory_barrier();

    virtio_net_notify(0);
}

static void setup_initial_tx(void) {
    volatile struct virtq_avail *tx_avail = (volatile struct virtq_avail *)(
        tx_queue_mem + sizeof(struct virtq_desc) * tx_queue_size);
    uint32_t used_off = virtq_used_offset(tx_queue_size);
    volatile struct virtq_used *tx_used = (volatile struct virtq_used *)(
        tx_queue_mem + used_off);

    tx_last_used_idx = 0;
    tx_avail->flags = 0;
    tx_avail->idx = 0;
    tx_used->flags = 0;
    tx_used->idx = 0;
}

#if defined(__x86_64__) || defined(__i386__)
static int init_pci(void) {
    const struct xscore_pci_device *dev = xscore_pci_find(VIRTIO_VENDOR_ID, VIRTIO_NET_DEVICE_ID);
    if (!dev) return 0;
    if (!dev->bar_is_io[0] || dev->bars[0] > UINT16_MAX - 0xff) return 0;
    if (!xscore_pci_enable_bus_master(dev)) return 0;

    pci_io_base = (uint16_t)dev->bars[0];
    transport = VIRTIO_TRANSPORT_PCI;

    port_out8((uint16_t)(pci_io_base + VIRTIO_PCI_STATUS), 0);
    port_out8((uint16_t)(pci_io_base + VIRTIO_PCI_STATUS),
              VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    uint32_t host_features = port_in32((uint16_t)(pci_io_base + VIRTIO_PCI_HOST_FEATURES));
    xscore_serial_write("[NET] VirtIO PCI host features: ");
    serial_hex(host_features);
    xscore_serial_write("\n");
    port_out32((uint16_t)(pci_io_base + VIRTIO_PCI_GUEST_FEATURES),
               host_features & ((1U << 5) | (1U << 27)));

    /* Queue 0 (RX) */
    port_out16((uint16_t)(pci_io_base + VIRTIO_PCI_QUEUE_SEL), 0);
    rx_queue_size = port_in16((uint16_t)(pci_io_base + VIRTIO_PCI_QUEUE_NUM));
    xscore_serial_write("[NET] rx_queue_size: ");
    serial_hex(rx_queue_size);
    xscore_serial_write("\n");
    if (rx_queue_size == 0 || rx_queue_size > 256) return 0;

    for (size_t i = 0; i < sizeof(rx_queue_mem); ++i) rx_queue_mem[i] = 0;
    uint64_t rx_phys = (uint64_t)(uintptr_t)rx_queue_mem;
    port_out32((uint16_t)(pci_io_base + VIRTIO_PCI_QUEUE_PFN), (uint32_t)(rx_phys >> 12));

    /* Queue 1 (TX) */
    port_out16((uint16_t)(pci_io_base + VIRTIO_PCI_QUEUE_SEL), 1);
    tx_queue_size = port_in16((uint16_t)(pci_io_base + VIRTIO_PCI_QUEUE_NUM));
    xscore_serial_write("[NET] tx_queue_size: ");
    serial_hex(tx_queue_size);
    xscore_serial_write("\n");
    if (tx_queue_size == 0 || tx_queue_size > 256) return 0;

    for (size_t i = 0; i < sizeof(tx_queue_mem); ++i) tx_queue_mem[i] = 0;
    uint64_t tx_phys = (uint64_t)(uintptr_t)tx_queue_mem;
    port_out32((uint16_t)(pci_io_base + VIRTIO_PCI_QUEUE_PFN), (uint32_t)(tx_phys >> 12));

    /* Read MAC */
    uint8_t test_mac[6];
    int valid_mac = 0;
    for (int i = 0; i < 6; ++i) {
        test_mac[i] = port_in8((uint16_t)(pci_io_base + VIRTIO_PCI_CONFIG + i));
        if (test_mac[i] != 0 && test_mac[i] != 0xff) valid_mac = 1;
    }
    if (valid_mac) {
        for (int i = 0; i < 6; ++i) net_mac[i] = test_mac[i];
    }
    xscore_serial_write("[NET] MAC address: ");
    for (int i = 0; i < 6; ++i) {
        serial_hex(net_mac[i]);
        if (i < 5) xscore_serial_write(":");
    }
    xscore_serial_write("\n");

    port_out8((uint16_t)(pci_io_base + VIRTIO_PCI_STATUS),
              VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    setup_initial_rx();
    setup_initial_tx();
    completion_irq = xscore_pci_enable_msix(dev, 43);
    if (completion_irq) {
        port_out16(pci_io_base + 0x14, UINT16_MAX);
        for (uint16_t queue = 0; queue < 2; ++queue) {
            port_out16(pci_io_base + VIRTIO_PCI_QUEUE_SEL, queue);
            port_out16(pci_io_base + 0x16, 0);
            if (port_in16(pci_io_base + 0x16) != 0) completion_irq = 0;
        }
        if (completion_irq) xscore_serial_write("[INFO] VirtIO-Net MSI-X completion enabled\n");
    }
    return 1;
}
#endif

#if defined(__aarch64__) || defined(__arm__) || defined(__riscv)
static int try_init_mmio(uint64_t base) {
    (void)xscore_vmm_map_page(base, base, XSCORE_PAGE_WRITABLE);

    if (mmio_read32(base, VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976) return 0;
    if (mmio_read32(base, VIRTIO_MMIO_DEVICE_ID) != 1) return 0;

    uint32_t version = mmio_read32(base, VIRTIO_MMIO_VERSION);
    if (version != 1 && version != 2) return 0;

    mmio_base = base;
    transport = VIRTIO_TRANSPORT_MMIO;

    mmio_write32(base, VIRTIO_MMIO_STATUS, 0);
    mmio_write32(base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    if (version >= 2) {
        mmio_write32(base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
        uint32_t f1 = mmio_read32(base, VIRTIO_MMIO_DEVICE_FEATURES);
        mmio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
        mmio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES, f1 & 1);

        mmio_write32(base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
        uint32_t f0 = mmio_read32(base, VIRTIO_MMIO_DEVICE_FEATURES);
        mmio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
        mmio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES, f0 & (1U << 5));

        mmio_write32(base, VIRTIO_MMIO_STATUS, 1 | 2 | 8);
        if (!(mmio_read32(base, VIRTIO_MMIO_STATUS) & 8)) return 0;
    }

    /* Queue 0 (RX) */
    mmio_write32(base, VIRTIO_MMIO_QUEUE_SEL, 0);
    uint32_t qmax0 = mmio_read32(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qmax0 == 0) return 0;
    rx_queue_size = qmax0 > 64 ? 64 : (uint16_t)qmax0;
    mmio_write32(base, VIRTIO_MMIO_QUEUE_NUM, rx_queue_size);

    for (size_t i = 0; i < sizeof(rx_queue_mem); ++i) rx_queue_mem[i] = 0;

    if (version == 1) {
        mmio_write32(base, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_PFN, (uint32_t)((uintptr_t)rx_queue_mem / 4096));
    } else {
        uintptr_t desc = (uintptr_t)rx_queue_mem;
        uintptr_t avail = (uintptr_t)(rx_queue_mem + sizeof(struct virtq_desc) * rx_queue_size);
        uintptr_t used = (uintptr_t)(rx_queue_mem + virtq_used_offset(rx_queue_size));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)desc >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)avail);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)((uint64_t)avail >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)used);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)((uint64_t)used >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_READY, 1);
    }

    /* Queue 1 (TX) */
    mmio_write32(base, VIRTIO_MMIO_QUEUE_SEL, 1);
    uint32_t qmax1 = mmio_read32(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qmax1 == 0) return 0;
    tx_queue_size = qmax1 > 64 ? 64 : (uint16_t)qmax1;
    mmio_write32(base, VIRTIO_MMIO_QUEUE_NUM, tx_queue_size);

    for (size_t i = 0; i < sizeof(tx_queue_mem); ++i) tx_queue_mem[i] = 0;

    if (version == 1) {
        mmio_write32(base, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_PFN, (uint32_t)((uintptr_t)tx_queue_mem / 4096));
    } else {
        uintptr_t desc = (uintptr_t)tx_queue_mem;
        uintptr_t avail = (uintptr_t)(tx_queue_mem + sizeof(struct virtq_desc) * tx_queue_size);
        uintptr_t used = (uintptr_t)(tx_queue_mem + virtq_used_offset(tx_queue_size));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)((uint64_t)desc >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)avail);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)((uint64_t)avail >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)used);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)((uint64_t)used >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_READY, 1);
    }

    /* Read MAC */
    uint8_t test_mac[6];
    int valid_mac = 0;
    for (int i = 0; i < 6; ++i) {
        test_mac[i] = *(volatile uint8_t *)(uintptr_t)(base + VIRTIO_MMIO_CONFIG + i);
        if (test_mac[i] != 0 && test_mac[i] != 0xff) valid_mac = 1;
    }
    if (valid_mac) {
        for (int i = 0; i < 6; ++i) net_mac[i] = test_mac[i];
    }

    mmio_write32(base, VIRTIO_MMIO_STATUS, version >= 2 ? (1 | 2 | 8 | 4) : (1 | 2 | 4));

    setup_initial_rx();
    setup_initial_tx();
    return 1;
}
#endif

int xscore_virtio_net_initialize(void) {
    transport = VIRTIO_TRANSPORT_NONE;
    pci_io_base = 0;
    mmio_base = 0;

#if defined(__x86_64__) || defined(__i386__)
    if (init_pci()) {
        xscore_serial_write("[INFO] VirtIO-Net PCI device initialized\n");
        return 1;
    }
#endif

#if defined(__aarch64__) || defined(__arm__)
    for (uint64_t base = UINT64_C(0x0a000000);
         base < UINT64_C(0x0a004000); base += UINT64_C(0x200)) {
        if (try_init_mmio(base)) {
            xscore_serial_write("[INFO] VirtIO-Net MMIO device initialized\n");
            return 1;
        }
    }
#elif defined(__riscv)
    for (uint64_t base = UINT64_C(0x10001000);
         base <= UINT64_C(0x10008000); base += UINT64_C(0x1000)) {
        if (try_init_mmio(base)) {
            xscore_serial_write("[INFO] VirtIO-Net MMIO device initialized\n");
            return 1;
        }
    }
#endif

    xscore_serial_write("[INFO] No VirtIO-Net device detected (using loopback mode)\n");
    return 0;
}

int xscore_virtio_net_probe(void) {
    return xscore_virtio_net_initialize();
}

int xscore_virtio_net_present(void) {
    return transport != VIRTIO_TRANSPORT_NONE;
}

void xscore_virtio_net_get_mac(uint8_t mac[6]) {
    if (mac) {
        for (int i = 0; i < 6; ++i) mac[i] = net_mac[i];
    }
}

int64_t xscore_virtio_net_transmit(const void *buf, uint64_t len) {
    if (transport == VIRTIO_TRANSPORT_NONE || !buf || len == 0 || len > 1514) return 0;

    uintptr_t lock_flags = xscore_spin_lock_irqsave(&virtio_net_tx_lock);
    if (transport == VIRTIO_TRANSPORT_NONE) {
        xscore_spin_unlock_irqrestore(&virtio_net_tx_lock, lock_flags);
        return 0;
    }

    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)tx_buffer;
    for (size_t i = 0; i < sizeof(struct virtio_net_hdr); ++i) {
        ((uint8_t *)hdr)[i] = 0;
    }
    const uint8_t *src = (const uint8_t *)buf;
    uint8_t *dst = tx_buffer + sizeof(struct virtio_net_hdr);
    for (uint64_t i = 0; i < len; ++i) {
        dst[i] = src[i];
    }

    volatile struct virtq_desc *tx_desc = (volatile struct virtq_desc *)tx_queue_mem;
    volatile struct virtq_avail *tx_avail = (volatile struct virtq_avail *)(
        tx_queue_mem + sizeof(struct virtq_desc) * tx_queue_size);
    uint32_t used_off = virtq_used_offset(tx_queue_size);
    volatile struct virtq_used *tx_used = (volatile struct virtq_used *)(
        tx_queue_mem + used_off);

    tx_desc[0].address = (uint64_t)(uintptr_t)tx_buffer;
    tx_desc[0].length = (uint32_t)(sizeof(struct virtio_net_hdr) + len);
    tx_desc[0].flags = 0;
    tx_desc[0].next = 0;

    uint16_t prev_used = tx_last_used_idx;
    uint16_t slot = tx_avail->idx % tx_queue_size;
    tx_avail->ring[slot] = 0;
    memory_barrier();
    tx_avail->idx++;
    memory_barrier();

    virtio_net_notify(1);

    uint64_t deadline = xscore_timer_ticks() + 5 * xscore_timer_frequency();
    for (uint32_t spin = 0; spin < 10000000; ++spin) {
        memory_barrier();
        if (tx_used->idx != prev_used) {
            tx_last_used_idx = tx_used->idx;
            xscore_spin_unlock_irqrestore(&virtio_net_tx_lock, lock_flags);
            return (int64_t)len;
        }
#if defined(__x86_64__) || defined(__i386__)
        if (completion_irq) {
            uintptr_t saved = xscore_irq_save();
            if (tx_used->idx == prev_used && xscore_timer_ticks() < deadline)
                __asm__ volatile("sti; hlt; cli" ::: "memory");
            xscore_irq_restore(saved);
            if (xscore_timer_ticks() >= deadline) break;
        }
#else
        if ((spin & 127) == 127) xscore_device_irq_wait();
        if (xscore_timer_ticks() >= deadline) break;
#endif
    }
    /* A late DMA completion must not race reuse of the single TX buffer. */
    transport = VIRTIO_TRANSPORT_NONE;
    xscore_serial_write("[WARN] virtio-net transmit timeout\n");
    xscore_spin_unlock_irqrestore(&virtio_net_tx_lock, lock_flags);
    return 0;
}

void xscore_virtio_net_poll_rx(void) {
    if (transport == VIRTIO_TRANSPORT_NONE) return;
    if (completion_irq) {
        uintptr_t saved = xscore_irq_save();
        int pending = __atomic_exchange_n(&rx_pending, 0, __ATOMIC_ACQUIRE);
        xscore_irq_restore(saved);
#if defined(__x86_64__) || defined(__i386__)
        /* A syscall may poll with IF clear; its MSI-X cannot run yet. */
        if (!pending && (saved & (1U << 9))) return;
#else
        (void)pending;
#endif
    }

    volatile struct virtq_desc *rx_desc = (volatile struct virtq_desc *)rx_queue_mem;
    volatile struct virtq_avail *rx_avail = (volatile struct virtq_avail *)(
        rx_queue_mem + sizeof(struct virtq_desc) * rx_queue_size);
    uint32_t used_off = virtq_used_offset(rx_queue_size);
    volatile struct virtq_used *rx_used = (volatile struct virtq_used *)(
        rx_queue_mem + used_off);

    memory_barrier();
    int received = 0;
    while (rx_last_used_idx != rx_used->idx) {
        uint16_t elem_idx = rx_last_used_idx % rx_queue_size;
        uint32_t desc_id = rx_used->ring[elem_idx].id;
        uint32_t len = rx_used->ring[elem_idx].length;

        if (desc_id < RX_BUFFER_COUNT && len > sizeof(struct virtio_net_hdr) &&
            len <= RX_BUFFER_SIZE) {
            uint32_t pkt_len = len - (uint32_t)sizeof(struct virtio_net_hdr);
            const uint8_t *pkt_data = rx_buffers[desc_id] + sizeof(struct virtio_net_hdr);
            xscore_net_queue_rx(pkt_data, pkt_len);
        }

        if (desc_id < RX_BUFFER_COUNT) {
            rx_desc[desc_id].address = (uint64_t)(uintptr_t)rx_buffers[desc_id];
            rx_desc[desc_id].length = sizeof(rx_buffers[desc_id]);
            rx_desc[desc_id].flags = VIRTQ_DESC_F_WRITE;
            rx_desc[desc_id].next = 0;

            uint16_t avail_slot = rx_avail->idx % rx_queue_size;
            rx_avail->ring[avail_slot] = (uint16_t)desc_id;
            memory_barrier();
            rx_avail->idx++;
            memory_barrier();
        }

        rx_last_used_idx++;
        received = 1;
        memory_barrier();
    }
    if (received) {
        virtio_net_notify(0);
    }
}
