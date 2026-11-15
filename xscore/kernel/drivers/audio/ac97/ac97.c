
#include "ac97.h"
#include <xscore/io.h>

void ac97_init(ac97_controller_t *controller) {
    // Cold reset AC97 controller
    out16(controller->nam_bar + 0x00, 0x0000); // Reset register
    // Wait for codec ready...
}

void ac97_write_codec(ac97_controller_t *controller, uint8_t reg, uint16_t value) {
    out16(controller->nam_bar + reg, value);
}

uint16_t ac97_read_codec(ac97_controller_t *controller, uint8_t reg) {
    return in16(controller->nam_bar + reg);
}

#define AC97_REG_BDBAR 0x00
#define AC97_REG_LVI   0x15
#define AC97_REG_SR    0x16

void ac97_start_bus_master(ac97_controller_t *controller, uint32_t bdl_addr) {
    out32(controller->nabm_bar + AC97_REG_BDBAR, bdl_addr);
    out8(controller->nabm_bar + AC97_REG_LVI, 0x1f); // Set Last Valid Index
    out16(controller->nabm_bar + AC97_REG_SR, 0x0001); // Start Bus Master
}
