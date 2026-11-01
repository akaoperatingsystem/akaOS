#ifndef XSCORE_SOUND_H
#define XSCORE_SOUND_H

#include <stdint.h>
#include <stddef.h>

struct xscore_sound_device {
    char driver_name[32];
    char chipset_name[64];
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t io_base;
    uint64_t mmio_base;
    uint8_t irq;
    uint32_t sample_rates;
    uint8_t channels;
    uint8_t bits_per_sample;
    int is_active;
};

void xscore_sound_initialize(void);
uint32_t xscore_sound_count(void);
const struct xscore_sound_device *xscore_sound_get(uint32_t index);
size_t xscore_sound_format_proc_asound(char *buf, size_t max_size);

#endif
