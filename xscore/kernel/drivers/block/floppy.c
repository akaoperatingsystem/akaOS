
#include <xscore/arch.h>
#include <xscore/ioctl.h>
#include <xscore/io.h>
#include <xscore/serial.h>
#include <xscore/usercopy.h>

#define FDC_BASE_PORT 0x3F0
#define FDC_IRQ 6
#define FDC_DMA_CHANNEL 2

// FDC Registers
#define FDC_MSR 0x3F4
#define FDC_FIFO 0x3F5

// FDC Command Set
#define FDC_CMD_SPECIFY 0x03
#define FDC_CMD_RECALIBRATE 0x07
#define FDC_CMD_SENSE_INT 0x08
#define FDC_CMD_SEEK 0x0F
#define FDC_CMD_READ 0x46
#define FDC_CMD_WRITE 0x45
#define FDC_CMD_FORMAT 0x4D

static uint8_t fdc_read_msr() {
    return port_in8(FDC_BASE_PORT + 4);
}

static void fdc_send_byte(uint8_t byte) {
    for (int i = 0; i < 1000; i++) {
        if (fdc_read_msr() & 0x80) {
            port_out8(FDC_BASE_PORT + 5, byte);
            return;
        }
    }
}

static uint8_t fdc_read_byte() {
    for (int i = 0; i < 1000; i++) {
        if (fdc_read_msr() & 0x80) {
            return port_in8(FDC_BASE_PORT + 5);
        }
    }
    return 0;
}

int floppy_init() {
    xscore_serial_write("[FLOPPY] Initializing FDC driver...\n");
    // Simple reset
    port_out8(FDC_BASE_PORT + 2, 0);
    port_out8(FDC_BASE_PORT + 2, 0x0C);
    
    // Specify
    fdc_send_byte(FDC_CMD_SPECIFY);
    fdc_send_byte(0xDF); // Step rate 3ms, head unload 240ms
    fdc_send_byte(0x02); // Head load 2ms, DMA enabled
    
    return 0;
}

int floppy_ioctl(int request, void *arg) {
    switch (request) {
        case AKAOS_BLKFORMAT:
            xscore_serial_write("[FLOPPY] Executing format command.\n");
            floppy_recalibrate(0);
            floppy_seek(0, 0);
            floppy_format_track(0, 0);
            return 0;
        default:
            return -1;
    }
}


#define DMA_ADDR_2 0x04
#define DMA_CNT_2  0x05
#define DMA_MASK   0x0A
#define DMA_MODE   0x0B
#define DMA_FF     0x0C

void floppy_dma_init(uint32_t addr, uint32_t size) {
    // 1. Mask channel 2
    port_out8(DMA_MASK, 0x06);
    // 2. Set mode: read/write/single mode
    port_out8(DMA_MODE, 0x46);
    // 3. Clear flip-flop
    port_out8(DMA_FF, 0x00);
    // 4. Set address
    port_out8(DMA_ADDR_2, (uint8_t)(addr & 0xFF));
    port_out8(DMA_ADDR_2, (uint8_t)((addr >> 8) & 0xFF));
    // 5. Set count
    port_out8(DMA_CNT_2, (uint8_t)((size - 1) & 0xFF));
    port_out8(DMA_CNT_2, (uint8_t)(((size - 1) >> 8) & 0xFF));
    // 6. Unmask channel 2
    port_out8(DMA_MASK, 0x02);
}


#include <xscore/storage.h>


void floppy_register() {
    struct xscore_storage_device dev;
    memset(&dev, 0, sizeof(dev));
    
    dev.type = XSCORE_STORAGE_FLOPPY;
    dev.active = 1; // REQUIRED: must be active to register
    strncpy(dev.name, "/dev/fd0", 31);
    strncpy(dev.model, "Floppy Disk Drive", 40);
    dev.sector_size = 512;
    dev.total_sectors = 2880; // 1.44MB floppy
    dev.format = floppy_storage_format;
    
    xscore_storage_register_device(&dev);
}




// Send a command to the FDC
static void fdc_send_command(uint8_t cmd) {
    fdc_send_byte(cmd);
}

// Recalibrate the drive
void floppy_recalibrate(uint8_t drive) {
    xscore_serial_write("[FLOPPY] Recalibrating...\n");
    fdc_send_byte(FDC_CMD_RECALIBRATE);
    fdc_send_byte(drive);
    // In a full implementation, wait for interrupt or check MSR
}

// Seek to a specific cylinder
void floppy_seek(uint8_t drive, uint8_t cylinder) {
    xscore_serial_write("[FLOPPY] Seeking to cylinder...\n");
    fdc_send_byte(FDC_CMD_SEEK);
    fdc_send_byte(drive);
    fdc_send_byte(cylinder);
}


void floppy_format_track(uint8_t drive, uint8_t track) {
    xscore_serial_write("[FLOPPY] Formatting track...");
    
    // 1. Setup DMA for format data
    // The format data usually requires 4 bytes per sector: C, H, R, N
    // For 1.44MB floppy: 18 sectors/track
    uint8_t format_data[4 * 18];
    for (int i = 0; i < 18; i++) {
        format_data[i*4] = track;      // Cylinder
        format_data[i*4+1] = 0;        // Head
        format_data[i*4+2] = i + 1;    // Sector
        format_data[i*4+3] = 2;        // Size (512 bytes)
    }
    
    floppy_dma_init((uint32_t)format_data, sizeof(format_data));
    
    // 2. Send FORMAT TRACK command
    fdc_send_byte(FDC_CMD_FORMAT);
    fdc_send_byte(drive);
    fdc_send_byte(2); // N=2 (512 bytes)
    fdc_send_byte(18); // 18 sectors
    fdc_send_byte(0x1B); // Gap length 3
    fdc_send_byte(0xE5); // Fill byte
    
    uint8_t st0, cyl;
    floppy_sense_interrupt(&st0, &cyl);
    xscore_serial_write(" Command completed.\n");
}


// Sense interrupt from FDC
uint8_t floppy_sense_interrupt(uint8_t *st0, uint8_t *cyl) {
    fdc_send_byte(FDC_CMD_SENSE_INT);
    *st0 = fdc_read_byte();
    *cyl = fdc_read_byte();
    return 0;
}


int floppy_storage_format(struct xscore_storage_device *dev, void *arg) {
    struct akaos_format_params params;
    if (xscore_copy_from_user(&params, arg, sizeof(params)) != 0) {
        return -AKAOS_EFAULT;
    }
    return floppy_ioctl(AKAOS_BLKFORMAT, &params);
}
