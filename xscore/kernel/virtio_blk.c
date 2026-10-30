#include <xscore/pci.h>
#include <xscore/smp.h>
#include <xscore/timer.h>
#include <xscore/serial.h>
#include <xscore/virtio_blk.h>
#include <stddef.h>
#include <stdint.h>

#define VIRTIO_VENDOR_ID UINT16_C(0x1af4)
#define VIRTIO_BLK_LEGACY_DEVICE_ID UINT16_C(0x1001)

#define VIRTIO_PCI_HOST_FEATURES  UINT16_C(0x00)
#define VIRTIO_PCI_GUEST_FEATURES UINT16_C(0x04)
#define VIRTIO_PCI_QUEUE_PFN      UINT16_C(0x08)
#define VIRTIO_PCI_QUEUE_NUM      UINT16_C(0x0c)
#define VIRTIO_PCI_QUEUE_SEL      UINT16_C(0x0e)
#define VIRTIO_PCI_QUEUE_NOTIFY   UINT16_C(0x10)
#define VIRTIO_PCI_STATUS         UINT16_C(0x12)
#define VIRTIO_PCI_ISR            UINT16_C(0x13)
#define VIRTIO_PCI_CONFIG         UINT16_C(0x14)

#define VIRTIO_STATUS_ACKNOWLEDGE UINT8_C(0x01)
#define VIRTIO_STATUS_DRIVER     UINT8_C(0x02)
#define VIRTIO_STATUS_DRIVER_OK  UINT8_C(0x04)
#define VIRTIO_STATUS_FAILED     UINT8_C(0x80)

#define VIRTQ_DESC_F_NEXT  UINT16_C(1)
#define VIRTQ_DESC_F_WRITE UINT16_C(2)
#define VIRTIO_QUEUE_MEMORY_BYTES UINT64_C(32768)
#define VIRTIO_MAX_QUEUE_SIZE UINT16_C(256)
#define VIRTIO_SECTOR_SIZE UINT64_C(512)
#define VIRTIO_REQUEST_TIMEOUT UINT64_C(10000000)
#define VIRTIO_BLK_T_IN  UINT32_C(0)
#define VIRTIO_BLK_T_OUT UINT32_C(1)
#define VIRTIO_F_ANY_LAYOUT (UINT32_C(1) << 27)

/* The x86 BIOS runner also exposes its writable sidecar as a conventional
 * primary IDE disk.  Keeping a tiny PIO fallback here makes persistence work
 * on firmware implementations that do not reliably expose legacy VirtIO DMA
 * completions to a 32-bit guest. */
#define ATA_PRIMARY_IO UINT16_C(0x1f0)
#define ATA_PRIMARY_CTRL UINT16_C(0x3f6)
#define ATA_STATUS_BSY UINT8_C(0x80)
#define ATA_STATUS_DRQ UINT8_C(0x08)
#define ATA_STATUS_ERR UINT8_C(0x01)
#define ATA_CMD_IDENTIFY UINT8_C(0xec)
#define ATA_CMD_READ_PIO UINT8_C(0x20)
#define ATA_CMD_WRITE_PIO UINT8_C(0x30)
#define ATA_CMD_FLUSH_CACHE UINT8_C(0xe7)
#define ATA_REQUEST_TIMEOUT UINT64_C(1000000)

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

struct virtio_blk_request {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
    uint8_t status;
} __attribute__((packed));

static uint8_t queue_memory[VIRTIO_QUEUE_MEMORY_BYTES]
    __attribute__((aligned(4096)));
static uint8_t sector_buffer[VIRTIO_SECTOR_SIZE]
    __attribute__((aligned(4096)));
static struct virtio_blk_request request;

static uint16_t io_base;
static uint16_t queue_size;
static uint64_t queue_used_offset;
static int completion_irq;
static uint64_t sector_count;
static int present;
static uint64_t ata_sector_count;
static int ata_present;

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

static inline void ata_delay(void) {
    (void)port_in8(ATA_PRIMARY_CTRL);
    (void)port_in8(ATA_PRIMARY_CTRL);
    (void)port_in8(ATA_PRIMARY_CTRL);
    (void)port_in8(ATA_PRIMARY_CTRL);
}

static void serial_u64(uint64_t value);
static inline uint16_t port_in16(uint16_t port);

static int ata_wait(uint8_t required, uint8_t forbidden) {
    for (uint64_t spin = 0; spin < ATA_REQUEST_TIMEOUT; ++spin) {
        uint8_t status = port_in8((uint16_t)(ATA_PRIMARY_IO + 7));
        if (status == UINT8_C(0xff) || (status & ATA_STATUS_ERR)) return 0;
        if ((status & forbidden) == 0 && (status & required) == required) return 1;
    }
    return 0;
}

static int ata_initialize(void) {
    ata_present = 0;
    ata_sector_count = 0;

    port_out8(ATA_PRIMARY_CTRL, 0);
    port_out8((uint16_t)(ATA_PRIMARY_IO + 6), UINT8_C(0xa0));
    ata_delay();
    uint8_t status = port_in8((uint16_t)(ATA_PRIMARY_IO + 7));
    if (status == UINT8_C(0xff) || status == 0) return 0;

    port_out8((uint16_t)(ATA_PRIMARY_IO + 2), 0);
    port_out8((uint16_t)(ATA_PRIMARY_IO + 3), 0);
    port_out8((uint16_t)(ATA_PRIMARY_IO + 4), 0);
    port_out8((uint16_t)(ATA_PRIMARY_IO + 5), 0);
    port_out8((uint16_t)(ATA_PRIMARY_IO + 7), ATA_CMD_IDENTIFY);
    status = port_in8((uint16_t)(ATA_PRIMARY_IO + 7));
    if (status == UINT8_C(0xff) || status == 0) return 0;
    if (!ata_wait(ATA_STATUS_DRQ, ATA_STATUS_BSY)) return 0;

    uint16_t identify[256];
    for (size_t index = 0; index < 256; ++index) identify[index] = port_in16(ATA_PRIMARY_IO);
    uint64_t sectors = ((uint64_t)identify[61] << 16) | identify[60];
    if (sectors == 0) {
        sectors = ((uint64_t)identify[103] << 48) |
                  ((uint64_t)identify[102] << 32) |
                  ((uint64_t)identify[101] << 16) | identify[100];
    }
    if (sectors == 0) return 0;
    ata_sector_count = sectors;
    ata_present = 1;
    xscore_serial_write("[INFO] ata-pio writable disk ready, sectors: ");
    serial_u64(sectors);
    xscore_serial_write("\n");
    return 1;
}

static int ata_transfer_sector(uint64_t sector, void *buffer, int write) {
    if (!ata_present || !buffer || sector >= ata_sector_count || sector > UINT64_C(0x0fffffff)) return 0;
    port_out8(ATA_PRIMARY_CTRL, 0);
    port_out8((uint16_t)(ATA_PRIMARY_IO + 6),
              (uint8_t)(UINT8_C(0xe0) | ((sector >> 24) & UINT64_C(0x0f))));
    port_out8((uint16_t)(ATA_PRIMARY_IO + 1), 0);
    port_out8((uint16_t)(ATA_PRIMARY_IO + 2), 1);
    port_out8((uint16_t)(ATA_PRIMARY_IO + 3), (uint8_t)sector);
    port_out8((uint16_t)(ATA_PRIMARY_IO + 4), (uint8_t)(sector >> 8));
    port_out8((uint16_t)(ATA_PRIMARY_IO + 5), (uint8_t)(sector >> 16));
    port_out8((uint16_t)(ATA_PRIMARY_IO + 7), write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);
    if (!ata_wait(ATA_STATUS_DRQ, ATA_STATUS_BSY)) return 0;

    if (write) {
        const uint8_t *source = (const uint8_t *)buffer;
        for (size_t index = 0; index < VIRTIO_SECTOR_SIZE / 2; ++index) {
            uint16_t word = (uint16_t)source[index * 2] |
                            (uint16_t)source[index * 2 + 1] << 8;
            port_out16(ATA_PRIMARY_IO, word);
        }
        if (!ata_wait(0, ATA_STATUS_BSY | ATA_STATUS_DRQ)) return 0;
        /* Commit the device write cache before reporting success to akaFS. */
        port_out8((uint16_t)(ATA_PRIMARY_IO + 7), ATA_CMD_FLUSH_CACHE);
        if (!ata_wait(0, ATA_STATUS_BSY)) return 0;
    } else {
        uint8_t *destination = (uint8_t *)buffer;
        for (size_t index = 0; index < VIRTIO_SECTOR_SIZE / 2; ++index) {
            uint16_t word = port_in16(ATA_PRIMARY_IO);
            destination[index * 2] = (uint8_t)word;
            destination[index * 2 + 1] = (uint8_t)(word >> 8);
        }
    }
    return 1;
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

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

static void clear_memory(void *memory, size_t length) {
    uint8_t *bytes = memory;
    for (size_t index = 0; index < length; ++index) bytes[index] = 0;
}

static void serial_u64(uint64_t value) {
    char digits[21];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value);
    while (count) {
        char text[2] = {digits[--count], 0};
        xscore_serial_write(text);
    }
}

static void fail_device(const char *reason) {
    xscore_serial_write("[WARN] virtio-blk unavailable: ");
    xscore_serial_write(reason);
    xscore_serial_write("\n");
    if (io_base) port_out8((uint16_t)(io_base + VIRTIO_PCI_STATUS),
                           VIRTIO_STATUS_FAILED);
    io_base = 0;
    present = 0;
}

int xscore_virtio_blk_initialize(void) {
    present = 0;
    io_base = 0;
    queue_size = 0;
    sector_count = 0;
    ata_present = 0;
    ata_sector_count = 0;

    const struct xscore_pci_device *device = xscore_pci_find(
        VIRTIO_VENDOR_ID, VIRTIO_BLK_LEGACY_DEVICE_ID);
    if (!device) {
        if (ata_initialize()) return 1;
        xscore_serial_write("[INFO] No writable block device detected\n");
        return 1;
    }
    if (!device->bar_is_io[0] || device->bars[0] > UINT16_MAX - 0xff) {
        fail_device("legacy I/O BAR is not available");
        return 0;
    }
    if (!xscore_pci_enable_bus_master(device)) {
        fail_device("PCI bus mastering could not be enabled");
        return 0;
    }
    io_base = (uint16_t)device->bars[0];

    port_out8((uint16_t)(io_base + VIRTIO_PCI_STATUS), 0);
    port_out8((uint16_t)(io_base + VIRTIO_PCI_STATUS),
              VIRTIO_STATUS_ACKNOWLEDGE);
    port_out8((uint16_t)(io_base + VIRTIO_PCI_STATUS),
              VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* Negotiate the legacy any-layout feature when the transitional device
     * offers it.  This is understood by both BIOS i440FX and UEFI q35 QEMU
     * machines and keeps the split-ring placement unambiguous. */
    uint32_t host_features =
        port_in32((uint16_t)(io_base + VIRTIO_PCI_HOST_FEATURES));
    port_out32((uint16_t)(io_base + VIRTIO_PCI_GUEST_FEATURES),
               host_features & VIRTIO_F_ANY_LAYOUT);

    port_out16((uint16_t)(io_base + VIRTIO_PCI_QUEUE_SEL), 0);
    queue_size = port_in16((uint16_t)(io_base + VIRTIO_PCI_QUEUE_NUM));
    if (queue_size < 3 || queue_size > VIRTIO_MAX_QUEUE_SIZE) {
        fail_device("unsupported request queue size");
        return 0;
    }
    queue_used_offset = align_up((uint64_t)sizeof(struct virtq_desc) * queue_size +
                                 sizeof(uint16_t) * 2 +
                                 sizeof(uint16_t) * queue_size, 4096);
    uint64_t used_end = queue_used_offset + sizeof(uint16_t) * 2 +
                        sizeof(struct virtq_used_elem) * queue_size;
    if (used_end > VIRTIO_QUEUE_MEMORY_BYTES) {
        fail_device("request queue does not fit kernel DMA area");
        return 0;
    }
    clear_memory(queue_memory, sizeof(queue_memory));
    uint64_t queue_physical = (uint64_t)(uintptr_t)queue_memory;
    if ((queue_physical & UINT64_C(0xfff)) != 0 ||
        queue_physical > ((uint64_t)UINT32_MAX << 12)) {
        fail_device("request queue is outside the legacy DMA range");
        return 0;
    }
    port_out32((uint16_t)(io_base + VIRTIO_PCI_QUEUE_PFN),
               (uint32_t)(queue_physical >> 12));

    uint32_t capacity_low = port_in32((uint16_t)(io_base + VIRTIO_PCI_CONFIG));
    uint32_t capacity_high = port_in32((uint16_t)(io_base + VIRTIO_PCI_CONFIG + 4));
    sector_count = ((uint64_t)capacity_high << 32) | capacity_low;
    if (sector_count == 0) {
        fail_device("device reports zero capacity");
        return 0;
    }
    port_out8((uint16_t)(io_base + VIRTIO_PCI_STATUS),
              VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                  VIRTIO_STATUS_DRIVER_OK);
    present = 1;
    completion_irq = xscore_pci_enable_msix(device, 42);
    if (completion_irq) {
        port_out16(io_base + 0x14, UINT16_MAX); /* No config-change vector. */
        port_out16(io_base + VIRTIO_PCI_QUEUE_SEL, 0);
        port_out16(io_base + 0x16, 0); /* Queue zero uses MSI-X table entry zero. */
        completion_irq = port_in16(io_base + 0x16) == 0;
        if (completion_irq) xscore_serial_write("[INFO] virtio-blk MSI-X completion enabled\n");
    }
    xscore_serial_write("[INFO] virtio-blk legacy device ready, sectors: ");
    serial_u64(sector_count);
    xscore_serial_write("\n");
    return 1;
}

int xscore_virtio_blk_present(void) {
    return present || ata_present;
}

uint64_t xscore_virtio_blk_sector_count(void) {
    return present ? sector_count : ata_sector_count;
}

static int transfer_sector(uint64_t sector, void *buffer, int write) {
    if (!present || !buffer || sector >= sector_count) return 0;
    if ((uint64_t)(uintptr_t)sector_buffer > UINT32_MAX ||
        (uint64_t)(uintptr_t)&request > UINT32_MAX) return 0;


    struct virtq_desc *descriptors = (struct virtq_desc *)queue_memory;
    volatile uint16_t *available_flags =
        (volatile uint16_t *)(queue_memory + sizeof(*descriptors) * queue_size);
    volatile uint16_t *available_index = available_flags + 1;
    volatile uint16_t *available_ring = available_index + 1;
    volatile uint16_t *used_flags =
        (volatile uint16_t *)(queue_memory + queue_used_offset);
    volatile uint16_t *used_index = used_flags + 1;
    volatile struct virtq_used_elem *used_ring =
        (volatile struct virtq_used_elem *)(used_index + 1);

    if (write) {
        const uint8_t *source = buffer;
        for (uint64_t index = 0; index < VIRTIO_SECTOR_SIZE; ++index) {
            sector_buffer[index] = source[index];
        }
    }

    request.type = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    request.reserved = 0;
    request.sector = sector;
    request.status = UINT8_C(0xff);
    descriptors[0] = (struct virtq_desc){
        (uint64_t)(uintptr_t)&request, 16, VIRTQ_DESC_F_NEXT, 1};
    descriptors[1] = (struct virtq_desc){
        (uint64_t)(uintptr_t)sector_buffer, VIRTIO_SECTOR_SIZE,
        (uint16_t)(VIRTQ_DESC_F_NEXT |
                   (write ? 0 : VIRTQ_DESC_F_WRITE)), 2};
    descriptors[2] = (struct virtq_desc){
        (uint64_t)(uintptr_t)&request.status, 1, VIRTQ_DESC_F_WRITE, 0};


    uint16_t old_used = *used_index;
    uint16_t slot = *available_index % queue_size;
    available_ring[slot] = 0;
    /*
     * Keep the descriptor visible before publishing the avail index.  An
     * empty compiler barrier is sufficient on x86 (which has a strong
     * store-ordering model), and unlike SFENCE it also works on the minimal
     * i386 CPUs exposed by BIOS QEMU machines that do not implement SSE.
     */
    __asm__ volatile("" : : : "memory");
    *available_index = (uint16_t)(*available_index + 1);
    __asm__ volatile("" : : : "memory");
    port_out16((uint16_t)(io_base + VIRTIO_PCI_QUEUE_NOTIFY), 0);
    /* Read back the device ISR register to flush the legacy PCI I/O write
     * before polling guest memory.  This matters on BIOS-emulated i386
     * machines where the notify write may otherwise remain posted while the
     * CPU spins on the used ring. */
    (void)port_in8((uint16_t)(io_base + VIRTIO_PCI_ISR));

    uint64_t deadline = xscore_timer_ticks() + 5 * xscore_timer_frequency();
    for (uint64_t spin = 0; spin < VIRTIO_REQUEST_TIMEOUT; ++spin) {
        __asm__ volatile("" ::: "memory");
        if (*used_index != old_used) break;
        if (completion_irq) {
            uintptr_t saved = xscore_irq_save();
            if (*used_index == old_used && xscore_timer_ticks() < deadline) {
                __asm__ volatile("sti; hlt; cli" ::: "memory");
            }
            xscore_irq_restore(saved);
            if (xscore_timer_ticks() >= deadline) break;
            continue;
        }
        __asm__ volatile("pause");
    }
    if (*used_index == old_used) {
        /* Do not reuse DMA buffers while the device may still own them. */
        present = 0;
        xscore_serial_write("[WARN] virtio-blk request timed out\n");
        return 0;
    }
    if (request.status != 0) {
        xscore_serial_write("[WARN] virtio-blk request returned status ");
        serial_u64(request.status);
        xscore_serial_write("\n");
        return 0;
    }
    volatile struct virtq_used_elem *element =
        &used_ring[(uint16_t)(old_used % queue_size)];
    uint32_t expected_length = write ? 1 : VIRTIO_SECTOR_SIZE + 1;
    if (element->id != 0 || element->length != expected_length) {
        xscore_serial_write("[WARN] virtio-blk used entry mismatch id=");
        serial_u64(element->id);
        xscore_serial_write(" length=");
        serial_u64(element->length);
        xscore_serial_write("\n");
        return 0;
    }
    if (!write) {
        uint8_t *destination = buffer;
        for (uint64_t index = 0; index < VIRTIO_SECTOR_SIZE; ++index) {
            destination[index] = sector_buffer[index];
        }
    }
    (void)port_in8((uint16_t)(io_base + VIRTIO_PCI_ISR));
    return 1;
}

int xscore_virtio_blk_read_sector(uint64_t sector, void *buffer) {
    if (!present && ata_present) return ata_transfer_sector(sector, buffer, 0);
    return transfer_sector(sector, buffer, 0);
}

int xscore_virtio_blk_write_sector(uint64_t sector, const void *buffer) {
    if (!present && ata_present) return ata_transfer_sector(sector, (void *)buffer, 1);
    return transfer_sector(sector, (void *)buffer, 1);
}
