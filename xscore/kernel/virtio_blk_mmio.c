#include <xscore/serial.h>
#include <xscore/virtio_blk.h>
#include <xscore/device_irq.h>
#include <xscore/timer.h>
#include <xscore/vmm.h>
#include <stddef.h>
#include <stdint.h>

#define VIRTIO_MMIO_MAGIC_VALUE       UINT32_C(0x000)
#define VIRTIO_MMIO_VERSION           UINT32_C(0x004)
#define VIRTIO_MMIO_DEVICE_ID         UINT32_C(0x008)
#define VIRTIO_MMIO_DEVICE_FEATURES   UINT32_C(0x010)
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL UINT32_C(0x014)
#define VIRTIO_MMIO_DRIVER_FEATURES   UINT32_C(0x020)
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL UINT32_C(0x024)
#define VIRTIO_MMIO_GUEST_PAGE_SIZE   UINT32_C(0x028)
#define VIRTIO_MMIO_QUEUE_SEL         UINT32_C(0x030)
#define VIRTIO_MMIO_QUEUE_NUM_MAX     UINT32_C(0x034)
#define VIRTIO_MMIO_QUEUE_NUM         UINT32_C(0x038)
#define VIRTIO_MMIO_QUEUE_ALIGN       UINT32_C(0x03c)
#define VIRTIO_MMIO_QUEUE_PFN         UINT32_C(0x040)
#define VIRTIO_MMIO_QUEUE_READY       UINT32_C(0x044)
#define VIRTIO_MMIO_QUEUE_NOTIFY      UINT32_C(0x050)
#define VIRTIO_MMIO_INTERRUPT_STATUS  UINT32_C(0x060)
#define VIRTIO_MMIO_INTERRUPT_ACK     UINT32_C(0x064)
#define VIRTIO_MMIO_STATUS            UINT32_C(0x070)
#define VIRTIO_MMIO_QUEUE_DESC_LOW    UINT32_C(0x080)
#define VIRTIO_MMIO_QUEUE_DESC_HIGH   UINT32_C(0x084)
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW   UINT32_C(0x090)
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH  UINT32_C(0x094)
#define VIRTIO_MMIO_QUEUE_USED_LOW    UINT32_C(0x0a0)
#define VIRTIO_MMIO_QUEUE_USED_HIGH   UINT32_C(0x0a4)
#define VIRTIO_MMIO_CONFIG            UINT32_C(0x100)

#define VIRTIO_MMIO_MAGIC UINT32_C(0x74726976)
#define VIRTIO_ID_BLOCK UINT32_C(2)
#define VIRTIO_STATUS_ACKNOWLEDGE UINT32_C(1)
#define VIRTIO_STATUS_DRIVER UINT32_C(2)
#define VIRTIO_STATUS_DRIVER_OK UINT32_C(4)
#define VIRTIO_STATUS_FEATURES_OK UINT32_C(8)
#define VIRTIO_STATUS_FAILED UINT32_C(128)
#define VIRTQ_DESC_F_NEXT UINT16_C(1)
#define VIRTQ_DESC_F_WRITE UINT16_C(2)
#define VIRTIO_BLK_T_IN UINT32_C(0)
#define VIRTIO_BLK_T_OUT UINT32_C(1)
#define VIRTIO_QUEUE_SIZE UINT16_C(8)
#define VIRTIO_QUEUE_MEMORY_BYTES 8192U
#define VIRTIO_SECTOR_SIZE 512U
#define VIRTIO_REQUEST_TIMEOUT UINT64_C(10000000)

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
static struct virtio_blk_request request __attribute__((aligned(8)));
static uint64_t mmio_base;
static uint64_t sector_count;
static uint16_t last_used_index;
static int present;

static inline uint32_t mmio_read32(uint64_t base, uint32_t offset) {
    return *(volatile uint32_t *)(uintptr_t)(base + offset);
}

static inline void mmio_write32(uint64_t base, uint32_t offset,
                                uint32_t value) {
    *(volatile uint32_t *)(uintptr_t)(base + offset) = value;
}

static inline void memory_barrier(void) {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("dmb sy" : : : "memory");
#elif defined(__riscv)
    __asm__ volatile("fence rw, rw" : : : "memory");
#else
    __asm__ volatile("" : : : "memory");
#endif
}

static void clear_bytes(void *address, size_t size) {
    uint8_t *bytes = address;
    for (size_t index = 0; index < size; ++index) bytes[index] = 0;
}

static void serial_u64(uint64_t value) {
    char digits[21];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        char text[2] = {digits[--count], 0};
        xscore_serial_write(text);
    }
}

static int initialize_at(uint64_t base) {
    (void)xscore_vmm_map_page(base, base, XSCORE_PAGE_WRITABLE |
                                         XSCORE_PAGE_NO_EXECUTE);
    if (mmio_read32(base, VIRTIO_MMIO_MAGIC_VALUE) != VIRTIO_MMIO_MAGIC ||
        mmio_read32(base, VIRTIO_MMIO_DEVICE_ID) != VIRTIO_ID_BLOCK) return 0;

    uint32_t version = mmio_read32(base, VIRTIO_MMIO_VERSION);
    if (version != 1 && version != 2) return 0;
    mmio_write32(base, VIRTIO_MMIO_STATUS, 0);
    mmio_write32(base, VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    if (version >= 2) {
        mmio_write32(base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
        (void)mmio_read32(base, VIRTIO_MMIO_DEVICE_FEATURES);
        mmio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
        mmio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES, 0);
        mmio_write32(base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
        uint32_t high_features = mmio_read32(base, VIRTIO_MMIO_DEVICE_FEATURES);
        if ((high_features & UINT32_C(1)) == 0) return 0;
        mmio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
        mmio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES, UINT32_C(1));
        mmio_write32(base, VIRTIO_MMIO_STATUS,
                     VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                         VIRTIO_STATUS_FEATURES_OK);
        if ((mmio_read32(base, VIRTIO_MMIO_STATUS) &
             VIRTIO_STATUS_FEATURES_OK) == 0) return 0;
    }

    mmio_write32(base, VIRTIO_MMIO_QUEUE_SEL, 0);
    uint32_t maximum = mmio_read32(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (maximum < VIRTIO_QUEUE_SIZE) return 0;
    mmio_write32(base, VIRTIO_MMIO_QUEUE_NUM, VIRTIO_QUEUE_SIZE);
    clear_bytes(queue_memory, sizeof(queue_memory));

    uintptr_t descriptor_address = (uintptr_t)queue_memory;
    uintptr_t available_address = descriptor_address +
                                  sizeof(struct virtq_desc) * VIRTIO_QUEUE_SIZE;
    uintptr_t used_address = descriptor_address + 4096U;
    if (version == 1) {
        mmio_write32(base, VIRTIO_MMIO_GUEST_PAGE_SIZE, 4096);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_ALIGN, 4096);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_PFN,
                     (uint32_t)(descriptor_address >> 12));
    } else {
        mmio_write32(base, VIRTIO_MMIO_QUEUE_DESC_LOW,
                     (uint32_t)descriptor_address);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_DESC_HIGH,
                     (uint32_t)((uint64_t)descriptor_address >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_AVAIL_LOW,
                     (uint32_t)available_address);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_AVAIL_HIGH,
                     (uint32_t)((uint64_t)available_address >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_USED_LOW,
                     (uint32_t)used_address);
        mmio_write32(base, VIRTIO_MMIO_QUEUE_USED_HIGH,
                     (uint32_t)((uint64_t)used_address >> 32));
        mmio_write32(base, VIRTIO_MMIO_QUEUE_READY, 1);
    }

    sector_count = (uint64_t)mmio_read32(base, VIRTIO_MMIO_CONFIG) |
                   ((uint64_t)mmio_read32(base, VIRTIO_MMIO_CONFIG + 4) << 32);
    if (sector_count == 0) {
        mmio_write32(base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
        return 0;
    }
    mmio_write32(base, VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                     (version >= 2 ? VIRTIO_STATUS_FEATURES_OK : 0) |
                     VIRTIO_STATUS_DRIVER_OK);
    mmio_base = base;
    last_used_index = 0;
    present = 1;
    return 1;
}

int xscore_virtio_blk_initialize(void) {
    mmio_base = 0;
    sector_count = 0;
    present = 0;
#if defined(__aarch64__) || defined(__arm__)
    for (uint64_t base = UINT64_C(0x0a000000);
         base < UINT64_C(0x0a004000); base += UINT64_C(0x200)) {
        if (initialize_at(base)) break;
    }
#elif defined(__riscv)
    for (uint64_t base = UINT64_C(0x10001000);
         base <= UINT64_C(0x10008000); base += UINT64_C(0x1000)) {
        if (initialize_at(base)) break;
    }
#endif
    if (!present) {
        xscore_serial_write("[INFO] No VirtIO MMIO block device detected\n");
        return 1;
    }
    xscore_serial_write("[INFO] VirtIO MMIO block device ready, sectors: ");
    serial_u64(sector_count);
    xscore_serial_write("\n");
    return 1;
}

int xscore_virtio_blk_present(void) {
    return present;
}

uint64_t xscore_virtio_blk_sector_count(void) {
    return sector_count;
}

static int transfer_sector(uint64_t sector, void *buffer, int write) {
    if (!present || !buffer || sector >= sector_count) return 0;
    volatile struct virtq_desc *descriptors =
        (volatile struct virtq_desc *)(void *)queue_memory;
    volatile uint16_t *available = (volatile uint16_t *)(void *)(
        queue_memory + sizeof(struct virtq_desc) * VIRTIO_QUEUE_SIZE);
    volatile uint16_t *available_index = available + 1;
    volatile uint16_t *available_ring = available + 2;
    volatile uint16_t *used =
        (volatile uint16_t *)(void *)(queue_memory + 4096U);
    volatile uint16_t *used_index = used + 1;
    volatile struct virtq_used_elem *used_ring =
        (volatile struct virtq_used_elem *)(void *)(used + 2);

    if (write) {
        const uint8_t *source = buffer;
        for (size_t index = 0; index < VIRTIO_SECTOR_SIZE; ++index) {
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

    uint16_t expected_used = *used_index;
    available_ring[*available_index % VIRTIO_QUEUE_SIZE] = 0;
    memory_barrier();
    *available_index = (uint16_t)(*available_index + 1);
    memory_barrier();
    mmio_write32(mmio_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    uint64_t spin = 0;
    uint64_t deadline = xscore_timer_ticks() + 5 * xscore_timer_frequency();
    while (*used_index == expected_used && spin++ < VIRTIO_REQUEST_TIMEOUT) {
        memory_barrier();
        if ((spin & 127) == 0) xscore_device_irq_wait();
        if (xscore_timer_ticks() >= deadline) break;
    }
    if (*used_index == expected_used) {
        present = 0; /* Quarantine the outstanding DMA buffer after timeout. */
        return 0;
    }
    memory_barrier();
    if (request.status != 0) return 0;
    volatile struct virtq_used_elem *result =
        &used_ring[last_used_index % VIRTIO_QUEUE_SIZE];
    if (result->id != 0) return 0;
    last_used_index = *used_index;

    if (!write) {
        uint8_t *destination = buffer;
        for (size_t index = 0; index < VIRTIO_SECTOR_SIZE; ++index) {
            destination[index] = sector_buffer[index];
        }
    }
    uint32_t interrupts = mmio_read32(mmio_base,
                                      VIRTIO_MMIO_INTERRUPT_STATUS);
    if (interrupts != 0) {
        mmio_write32(mmio_base, VIRTIO_MMIO_INTERRUPT_ACK, interrupts);
    }
    return 1;
}

int xscore_virtio_blk_read_sector(uint64_t sector, void *buffer) {
    return transfer_sector(sector, buffer, 0);
}

int xscore_virtio_blk_write_sector(uint64_t sector, const void *buffer) {
    return transfer_sector(sector, (void *)buffer, 1);
}
