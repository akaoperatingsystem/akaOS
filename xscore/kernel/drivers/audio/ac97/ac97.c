
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
