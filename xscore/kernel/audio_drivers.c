#include <xscore/sound.h>
#include <xscore/pci.h>
#include <xscore/serial.h>
void *memset(void *destination, int value, size_t count);

#define MAX_SOUND_CARDS 4
static struct xscore_sound_device g_sound_cards[MAX_SOUND_CARDS];
static uint32_t g_sound_count = 0;

static void str_copy(char *dst, size_t max, const char *src) {
    size_t i = 0;
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void xscore_sound_initialize(void) {
    if (g_sound_count > 0) return;
    memset(g_sound_cards, 0, sizeof(g_sound_cards));

    /* 1. Probe PCI for Audio Class (0x0401, 0x0403) */
    uint32_t pci_total = xscore_pci_device_count();
    for (uint32_t i = 0; i < pci_total && g_sound_count < MAX_SOUND_CARDS - 1; ++i) {
        const struct xscore_pci_device *dev = xscore_pci_get_device(i);
        if (!dev) continue;

        if (dev->class_code == 0x04 && (dev->subclass == 0x03 || dev->subclass == 0x01)) {
            struct xscore_sound_device *snd = &g_sound_cards[g_sound_count++];
            snd->vendor_id = dev->vendor_id;
            snd->device_id = dev->device_id;
            snd->is_active = 1;

            for (int b = 0; b < 6; ++b) {
                if (dev->bars[b]) {
                    if (dev->bar_is_io[b] && !snd->io_base) {
                        snd->io_base = dev->bars[b];
                    } else if (!dev->bar_is_io[b] && !snd->mmio_base) {
                        snd->mmio_base = dev->bars[b];
                    }
                }
            }

            if (dev->vendor_id == 0x8086 && dev->subclass == 0x03) {
                str_copy(snd->driver_name, sizeof(snd->driver_name), "Intel HDA Driver");
                str_copy(snd->chipset_name, sizeof(snd->chipset_name), "Intel High Definition Audio (Azalia)");
                snd->sample_rates = 192000;
                snd->channels = 8;
                snd->bits_per_sample = 32;
            } else if (dev->vendor_id == 0x1274) {
                str_copy(snd->driver_name, sizeof(snd->driver_name), "ES1371 Audio Driver");
                str_copy(snd->chipset_name, sizeof(snd->chipset_name), "Ensoniq AudioPCI ES1371 / CT5880");
                snd->sample_rates = 48000;
                snd->channels = 2;
                snd->bits_per_sample = 16;
            } else if (dev->vendor_id == 0x1af4 && dev->device_id == 0x1059) {
                str_copy(snd->driver_name, sizeof(snd->driver_name), "VirtIO-Sound Driver");
                str_copy(snd->chipset_name, sizeof(snd->chipset_name), "VirtIO Hardware Sound Device");
                snd->sample_rates = 96000;
                snd->channels = 2;
                snd->bits_per_sample = 24;
            } else {
                str_copy(snd->driver_name, sizeof(snd->driver_name), "AC'97 Audio Driver");
                str_copy(snd->chipset_name, sizeof(snd->chipset_name), "Generic AC'97 Audio Controller");
                snd->sample_rates = 48000;
                snd->channels = 2;
                snd->bits_per_sample = 16;
            }

            xscore_serial_write("[INFO] Sound hardware detected: ");
            xscore_serial_write(snd->chipset_name);
            xscore_serial_write("\n");
        }
    }

    /* The PIT/PC-speaker exists only on PC-compatible x86 machines. Do not
     * advertise it on ARM or RISC-V merely to manufacture feature parity. */
#if defined(__x86_64__) || defined(__i386__)
    if (g_sound_count < MAX_SOUND_CARDS) {
        struct xscore_sound_device *snd = &g_sound_cards[g_sound_count++];
        str_copy(snd->driver_name, sizeof(snd->driver_name), "PC Speaker Driver");
        str_copy(snd->chipset_name, sizeof(snd->chipset_name), "Intel 8254 PIT Tone Synthesizer");
        snd->channels = 1;
        snd->bits_per_sample = 8;
        snd->sample_rates = 44100;
        snd->is_active = (g_sound_count == 1); /* Active if primary */
    }
#endif

    xscore_serial_write(g_sound_count ? "[INFO] Audio drivers initialized\n"
                                      : "[INFO] No audio hardware detected\n");
}

uint32_t xscore_sound_count(void) {
    return g_sound_count;
}

const struct xscore_sound_device *xscore_sound_get(uint32_t index) {
    if (index < g_sound_count) return &g_sound_cards[index];
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

size_t xscore_sound_format_proc_asound(char *buf, size_t max_size) {
    if (!buf || max_size == 0) return 0;
    if (g_sound_count == 0) xscore_sound_initialize();

    size_t off = 0;
    for (uint32_t i = 0; i < g_sound_count; ++i) {
        const struct xscore_sound_device *snd = &g_sound_cards[i];
        off = app_str(buf, max_size, off, " ");
        off = app_u32(buf, max_size, off, i);
        off = app_str(buf, max_size, off, " [");
        off = app_str(buf, max_size, off, snd->driver_name);
        off = app_str(buf, max_size, off, "]: ");
        off = app_str(buf, max_size, off, snd->chipset_name);
        off = app_str(buf, max_size, off, "\n                      Channels: ");
        off = app_u32(buf, max_size, off, snd->channels);
        off = app_str(buf, max_size, off, ", Max Rate: ");
        off = app_u32(buf, max_size, off, snd->sample_rates);
        off = app_str(buf, max_size, off, " Hz, Bits: ");
        off = app_u32(buf, max_size, off, snd->bits_per_sample);
        if (snd->mmio_base) {
            off = app_str(buf, max_size, off, ", MMIO: 0x");
            off = app_hex(buf, max_size, off, snd->mmio_base, 8);
        } else if (snd->io_base) {
            off = app_str(buf, max_size, off, ", I/O: 0x");
            off = app_hex(buf, max_size, off, snd->io_base, 4);
        }
        off = app_str(buf, max_size, off, "\n");
    }

    return off;
}
