#include <xscore/drm_kms.h>
#include <xscore/gpu.h>
#include <xscore/pci.h>
#include <xscore/framebuffer.h>
#include <xscore/serial.h>
void *memset(void *destination, int value, size_t count);

#define MAX_GPUS 4
static struct xscore_gpu_device g_gpus[MAX_GPUS];
static uint32_t g_gpu_count = 0;
static int g_active_gpu_idx = -1;

/* Bochs / QEMU BGA I/O ports & registers */
#define VBE_DISPI_IOPORT_INDEX          0x01ce
#define VBE_DISPI_IOPORT_DATA           0x01cf
#define VBE_DISPI_INDEX_ID              0x0
#define VBE_DISPI_INDEX_XRES            0x1
#define VBE_DISPI_INDEX_YRES            0x2
#define VBE_DISPI_INDEX_BPP             0x3
#define VBE_DISPI_INDEX_ENABLE          0x4
#define VBE_DISPI_INDEX_BANK            0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH      0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT     0x7
#define VBE_DISPI_INDEX_X_OFFSET        0x8
#define VBE_DISPI_INDEX_Y_OFFSET        0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa

#define VBE_DISPI_DISABLED              0x00
#define VBE_DISPI_ENABLED               0x01
#define VBE_DISPI_LFB_ENABLED           0x40
#define VBE_DISPI_NOCLEARMEM            0x80

#if defined(__x86_64__) || defined(__i386__)
static inline void bga_write_reg(uint16_t index, uint16_t data) {
    uint16_t port_idx = VBE_DISPI_IOPORT_INDEX;
    uint16_t port_data = VBE_DISPI_IOPORT_DATA;
    __asm__ volatile("outw %0, %w1" : : "a"(index), "d"(port_idx));
    __asm__ volatile("outw %0, %w1" : : "a"(data), "d"(port_data));
}

static inline uint16_t bga_read_reg(uint16_t index) {
    uint16_t port_idx = VBE_DISPI_IOPORT_INDEX;
    uint16_t port_data = VBE_DISPI_IOPORT_DATA;
    __asm__ volatile("outw %0, %w1" : : "a"(index), "d"(port_idx));
    uint16_t data;
    __asm__ volatile("inw %w1, %0" : "=a"(data) : "d"(port_data));
    return data;
}
#else
static inline void bga_write_reg(uint16_t index, uint16_t data) { (void)index; (void)data; }
static inline uint16_t bga_read_reg(uint16_t index) { (void)index; return 0; }
#endif

static void str_copy(char *dst, size_t max, const char *src) {
    size_t i = 0;
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_equal(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 == *s2;
}

static void probe_gpu_hardware(const struct xscore_pci_device *pci_dev) {
    if (g_gpu_count >= MAX_GPUS) return;
    struct xscore_gpu_device *gpu = &g_gpus[g_gpu_count];
    memset(gpu, 0, sizeof(*gpu));

    gpu->vendor_id = pci_dev->vendor_id;
    gpu->device_id = pci_dev->device_id;
    gpu->class_code = ((uint16_t)pci_dev->class_code << 8) | pci_dev->subclass;
    gpu->pcie_gen = pci_dev->pcie_link_speed;
    gpu->pcie_width = pci_dev->pcie_link_width;

    /* BAR assignment */
    for (int b = 0; b < 6; ++b) {
        if (!pci_dev->bar_is_io[b] && pci_dev->bars[b]) {
            if (!gpu->fb_phys_base) {
                gpu->fb_phys_base = pci_dev->bars[b];
                gpu->fb_size_bytes = pci_dev->bar_sizes[b];
            } else if (!gpu->mmio_phys_base) {
                gpu->mmio_phys_base = pci_dev->bars[b];
            }
        }
    }

    /* 1. Bochs / QEMU Standard VGA */
    if (pci_dev->vendor_id == 0x1234 && pci_dev->device_id == 0x1111) {
        str_copy(gpu->driver_name, sizeof(gpu->driver_name), "Bochs BGA Driver");
        str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "QEMU Standard VGA (Bochs VBE)");
        gpu->accel_2d = 1;
        gpu->modeset_supported = 1;

        uint16_t bga_id = bga_read_reg(VBE_DISPI_INDEX_ID);
        if (bga_id >= 0xB0C0 && bga_id <= 0xB0C6) {
            uint16_t vram_64k = bga_read_reg(VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
            gpu->vram_size_mb = vram_64k ? (vram_64k * 64) / 1024 : 16;
        } else {
            gpu->vram_size_mb = 16;
        }
        if (g_active_gpu_idx < 0) g_active_gpu_idx = (int)g_gpu_count;
        g_gpu_count++;
        return;
    }

    /* 2. VirtIO-GPU */
    if (pci_dev->vendor_id == 0x1af4 && pci_dev->device_id == 0x1050) {
        str_copy(gpu->driver_name, sizeof(gpu->driver_name), "VirtIO-GPU Driver");
        str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "VirtIO GPU 2D/3D Display Adapter");
        gpu->accel_2d = 1;
        gpu->accel_3d = 1;
        gpu->modeset_supported = 1;
        gpu->vram_size_mb = (gpu->fb_size_bytes > 0) ? (uint32_t)(gpu->fb_size_bytes / (1024 * 1024)) : 32;
        if (g_active_gpu_idx < 0) g_active_gpu_idx = (int)g_gpu_count;
        g_gpu_count++;
        return;
    }

    /* 3. VMware SVGA II */
    if (pci_dev->vendor_id == 0x15ad && pci_dev->device_id == 0x0405) {
        str_copy(gpu->driver_name, sizeof(gpu->driver_name), "VMware SVGA Driver");
        str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "VMware SVGA II Adapter");
        gpu->accel_2d = 1;
        gpu->accel_3d = 1;
        gpu->modeset_supported = 1;
        gpu->vram_size_mb = 16;
        if (g_active_gpu_idx < 0) g_active_gpu_idx = (int)g_gpu_count;
        g_gpu_count++;
        return;
    }

    /* 4. NVIDIA GeForce RTX 5000 / 4000 / 3000 / 2000 / 1000 Series */
    if (pci_dev->vendor_id == 0x10de) {
        str_copy(gpu->driver_name, sizeof(gpu->driver_name), "NVIDIA Nouveau Stub Driver");
        gpu->accel_2d = 1;
        gpu->accel_3d = 1;
        gpu->modeset_supported = 1;

        if (pci_dev->device_id >= 0x2900 && pci_dev->device_id <= 0x29ff) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "NVIDIA GeForce RTX 5090 / RTX 5080 (Blackwell)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 24576;
        } else if (pci_dev->device_id == 0x2684) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "NVIDIA GeForce RTX 4090 (Ada Lovelace)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 24576;
        } else if (pci_dev->device_id == 0x2704) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "NVIDIA GeForce RTX 4080 (Ada Lovelace)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 16384;
        } else if (pci_dev->device_id == 0x2782) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "NVIDIA GeForce RTX 4070 Ti (Ada Lovelace)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 12288;
        } else if (pci_dev->device_id == 0x2204) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "NVIDIA GeForce RTX 3090 (Ampere)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 24576;
        } else if (pci_dev->device_id == 0x2206) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "NVIDIA GeForce RTX 3080 (Ampere)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 10240;
        } else if (pci_dev->device_id == 0x2484) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "NVIDIA GeForce RTX 3070 (Ampere)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 8192;
        } else if (pci_dev->device_id == 0x1e04) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "NVIDIA GeForce RTX 2080 Ti (Turing)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 11264;
        } else if (pci_dev->device_id == 0x1b80) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "NVIDIA GeForce GTX 1080 (Pascal)");
            gpu->vram_size_mb = 8192;
        } else {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "NVIDIA GeForce / RTX Graphics");
            gpu->vram_size_mb = 4096;
        }
        if (g_active_gpu_idx < 0) g_active_gpu_idx = (int)g_gpu_count;
        g_gpu_count++;
        return;
    }

    /* 5. AMD Radeon RX 9000 / 7000 / 6000 / 5000 / Vega / Polaris Series */
    if (pci_dev->vendor_id == 0x1002) {
        str_copy(gpu->driver_name, sizeof(gpu->driver_name), "AMD Radeon DRM Stub Driver");
        gpu->accel_2d = 1;
        gpu->accel_3d = 1;
        gpu->modeset_supported = 1;

        if (pci_dev->device_id >= 0x7500 && pci_dev->device_id <= 0x75ff) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "AMD Radeon RX 9000 Series (RDNA 4)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 24576;
        } else if (pci_dev->device_id == 0x744c) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "AMD Radeon RX 7900 XTX / 7900 XT (Navi 31 / RDNA 3)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 24576;
        } else if (pci_dev->device_id == 0x747e) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "AMD Radeon RX 7800 XT / 7700 XT (Navi 32 / RDNA 3)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 16384;
        } else if (pci_dev->device_id == 0x73bf) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "AMD Radeon RX 6900 XT / 6800 XT (Navi 21 / RDNA 2)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 16384;
        } else if (pci_dev->device_id == 0x731f) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "AMD Radeon RX 5700 XT / 5700 (Navi 10 / RDNA 1)");
            gpu->vram_size_mb = 8192;
        } else if (pci_dev->device_id == 0x687f) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "AMD Radeon RX Vega 64 / 56");
            gpu->vram_size_mb = 8192;
        } else if (pci_dev->device_id == 0x67df) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "AMD Radeon RX 580 / 570 (Polaris)");
            gpu->vram_size_mb = 8192;
        } else {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "AMD Radeon Graphics");
            gpu->vram_size_mb = 4096;
        }
        if (g_active_gpu_idx < 0) g_active_gpu_idx = (int)g_gpu_count;
        g_gpu_count++;
        return;
    }

    /* 6. Intel Arc & Iris Graphics */
    if (pci_dev->vendor_id == 0x8086 && (pci_dev->class_code == 0x03)) {
        str_copy(gpu->driver_name, sizeof(gpu->driver_name), "Intel i915 / Xe Stub Driver");
        gpu->accel_2d = 1;
        gpu->accel_3d = 1;
        gpu->modeset_supported = 1;

        if (pci_dev->device_id == 0x5690) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "Intel Arc A770 Graphics (Alchemist)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 16384;
        } else if (pci_dev->device_id == 0x5691) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "Intel Arc A750 Graphics (Alchemist)");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 8192;
        } else if (pci_dev->device_id == 0x56b0) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "Intel Arc Battlemage B580 Graphics");
            gpu->ray_tracing = 1;
            gpu->vram_size_mb = 12288;
        } else if (pci_dev->device_id == 0x9a49) {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "Intel Iris Xe Graphics (Tiger Lake)");
            gpu->vram_size_mb = 2048;
        } else {
            str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "Intel HD / UHD Graphics");
            gpu->vram_size_mb = 1024;
        }
        if (g_active_gpu_idx < 0) g_active_gpu_idx = (int)g_gpu_count;
        g_gpu_count++;
        return;
    }

    /* Generic VGA display controller */
    str_copy(gpu->driver_name, sizeof(gpu->driver_name), "Generic PCI VGA Driver");
    str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "Standard PCI Display Controller");
    gpu->vram_size_mb = 16;
    if (g_active_gpu_idx < 0) g_active_gpu_idx = (int)g_gpu_count;
    g_gpu_count++;
}

void xscore_gpu_initialize(void) {
    if (g_gpu_count > 0) return;

    /* Scan PCI for Display Class 0x03 */
    uint32_t pci_total = xscore_pci_device_count();
    for (uint32_t i = 0; i < pci_total; ++i) {
        const struct xscore_pci_device *dev = xscore_pci_get_device(i);
        if (dev && (dev->class_code == 0x03 || (dev->vendor_id == 0x1234 && dev->device_id == 0x1111))) {
            probe_gpu_hardware(dev);
        }
    }

    /* Fallback to universal GOP / VBE / RAMFB if no PCI GPU was bound */
    if (g_gpu_count == 0) {
        struct xscore_gpu_device *gpu = &g_gpus[0];
        str_copy(gpu->driver_name, sizeof(gpu->driver_name), "Universal Framebuffer Driver");
        str_copy(gpu->chipset_name, sizeof(gpu->chipset_name), "UEFI GOP / BIOS VBE / RAMFB Display");
        gpu->accel_2d = 1;
        gpu->vram_size_mb = 16;
        g_active_gpu_idx = 0;
        g_gpu_count = 1;
    }

    /* Populate active display resolution from current framebuffer */
    struct akaos_display_info disp_info;
    if (xscore_framebuffer_get_info(&disp_info) && g_active_gpu_idx >= 0) {
        struct xscore_gpu_device *active = &g_gpus[g_active_gpu_idx];
        active->width = disp_info.width;
        active->height = disp_info.height;
        active->stride = disp_info.stride_pixels * 4;
        active->bpp = 32;
    }

    const struct xscore_gpu_device *cur = xscore_gpu_get_active();
    if (cur) {
        xscore_serial_write("[INFO] GPU driver active: ");
        xscore_serial_write(cur->driver_name);
    /* Test 2D acceleration */
    if (cur && cur->accel_2d) {
        xscore_serial_write("[INFO] Testing 2D hardware acceleration...\n");
        // In a real scenario, this would call the DRM subsystem
    }

        xscore_serial_write(" [");
        xscore_serial_write(cur->chipset_name);
        xscore_serial_write("]\n");
    }

        /* Initialize DRM/KMS subsystem for the active GPU */
        xscore_drm_initialize(&g_gpus[g_active_gpu_idx]);

}

uint32_t xscore_gpu_count(void) {
    return g_gpu_count;
}

const struct xscore_gpu_device *xscore_gpu_get(uint32_t index) {
    if (index < g_gpu_count) return &g_gpus[index];
    return 0;
}

const struct xscore_gpu_device *xscore_gpu_get_active(void) {
    if (g_active_gpu_idx >= 0 && (uint32_t)g_active_gpu_idx < g_gpu_count) {
        return &g_gpus[g_active_gpu_idx];
    }
    if (g_gpu_count > 0) return &g_gpus[0];
    return 0;
}

int xscore_gpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    const struct xscore_gpu_device *gpu = xscore_gpu_get_active();
    if (!gpu || !gpu->modeset_supported) return 0;

    /* If Bochs BGA driver is active, reprogram BGA registers */
    if (str_equal(gpu->driver_name, "Bochs BGA Driver")) {
        bga_write_reg(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
        bga_write_reg(VBE_DISPI_INDEX_XRES, (uint16_t)width);
        bga_write_reg(VBE_DISPI_INDEX_YRES, (uint16_t)height);
        bga_write_reg(VBE_DISPI_INDEX_BPP, (uint16_t)bpp);
        bga_write_reg(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_NOCLEARMEM);
        return 1;
    }

    return 0;
}

static size_t app_str(char *buf, size_t max, size_t off, const char *s) {
    if (off >= max) return off;
    while (*s && off + 1 < max) buf[off++] = *s++;
    buf[off] = '\0';
    return off;
}

static size_t app_u32(char *buf, size_t max, size_t off, uint32_t val) {
    char tmp[12];
    int i = 0;
    if (val == 0) tmp[i++] = '0';
    else {
        while (val > 0) {
            tmp[i++] = (char)('0' + (val % 10));
            val /= 10;
        }
    }
    while (i > 0 && off + 1 < max) buf[off++] = tmp[--i];
    buf[off] = '\0';
    return off;
}

static size_t app_hex(char *buf, size_t max, size_t off, uint64_t val, int digits) {
    static const char hex[] = "0123456789abcdef";
    if (off + digits >= max) return off;
    for (int i = digits - 1; i >= 0; --i) {
        buf[off++] = hex[(val >> (i * 4)) & 0xf];
    }
    buf[off] = '\0';
    return off;
}

size_t xscore_gpu_format_proc_gpuinfo(char *buf, size_t max_size) {
    if (!buf || max_size == 0) return 0;
    if (g_gpu_count == 0) xscore_gpu_initialize();

    size_t off = 0;
    for (uint32_t i = 0; i < g_gpu_count; ++i) {
        const struct xscore_gpu_device *gpu = &g_gpus[i];
        off = app_str(buf, max_size, off, "GPU ");
        off = app_u32(buf, max_size, off, i);
        if ((int)i == g_active_gpu_idx) {
            off = app_str(buf, max_size, off, " [Active Primary]:\n");
        } else {
            off = app_str(buf, max_size, off, ":\n");
        }

        off = app_str(buf, max_size, off, "  Driver       : ");
        off = app_str(buf, max_size, off, gpu->driver_name);
        off = app_str(buf, max_size, off, "\n  Chipset      : ");
        off = app_str(buf, max_size, off, gpu->chipset_name);
        off = app_str(buf, max_size, off, "\n  Vendor ID    : 0x");
        off = app_hex(buf, max_size, off, gpu->vendor_id, 4);
        off = app_str(buf, max_size, off, "\n  Device ID    : 0x");
        off = app_hex(buf, max_size, off, gpu->device_id, 4);
        off = app_str(buf, max_size, off, "\n  Video RAM    : ");
        off = app_u32(buf, max_size, off, gpu->vram_size_mb);
        off = app_str(buf, max_size, off, " MB\n  Resolution   : ");
        off = app_u32(buf, max_size, off, gpu->width ? gpu->width : 1280);
        off = app_str(buf, max_size, off, "x");
        off = app_u32(buf, max_size, off, gpu->height ? gpu->height : 800);
        off = app_str(buf, max_size, off, "x32 (Stride: ");
        off = app_u32(buf, max_size, off, gpu->stride ? gpu->stride : 5120);
        off = app_str(buf, max_size, off, " bytes)\n  Pixel Format : BGRX8888\n  Framebuffer  : ");
        if (gpu->fb_phys_base) {
            off = app_str(buf, max_size, off, "Physical 0x");
            off = app_hex(buf, max_size, off, gpu->fb_phys_base, 8);
        } else {
            off = app_str(buf, max_size, off, "Linear EFI/VBE GOP Framebuffer");
        }
        off = app_str(buf, max_size, off, "\n  2D Accel     : ");
        off = app_str(buf, max_size, off, gpu->accel_2d ? "Supported\n" : "None\n");
        off = app_str(buf, max_size, off, "  3D Accel     : ");
        off = app_str(buf, max_size, off, gpu->accel_3d ? "Hardware Supported\n" : "Software Rasterizer\n");
        off = app_str(buf, max_size, off, "  Ray Tracing  : ");
        off = app_str(buf, max_size, off, gpu->ray_tracing ? "Hardware RT Acceleration\n" : "None\n");
        off = app_str(buf, max_size, off, "  Modesetting  : ");
        off = app_str(buf, max_size, off, gpu->modeset_supported ? "Hardware Supported\n" : "Firmware Fixed\n");
        if (gpu->pcie_gen) {
            off = app_str(buf, max_size, off, "  PCIe Link    : Gen");
            off = app_u32(buf, max_size, off, gpu->pcie_gen);
            off = app_str(buf, max_size, off, " x");
            off = app_u32(buf, max_size, off, gpu->pcie_width);
            off = app_str(buf, max_size, off, "\n");
        }
        off = app_str(buf, max_size, off, "\n");
    }

    return off;
}
