
#ifndef AC97_DRIVER_H
#define AC97_DRIVER_H

#include <stdint.h>

typedef struct {
    uint16_t nam_bar; // Native Audio Mixer Base Address
    uint16_t nabm_bar; // Native Audio Bus Master Base Address
} ac97_controller_t;

void ac97_init(ac97_controller_t *controller);
void ac97_write_codec(ac97_controller_t *controller, uint8_t reg, uint16_t value);
uint16_t ac97_read_codec(ac97_controller_t *controller, uint8_t reg);

#endif
