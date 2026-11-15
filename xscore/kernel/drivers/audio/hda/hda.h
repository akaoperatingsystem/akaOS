
#ifndef HDA_DRIVER_H
#define HDA_DRIVER_H

#include <stdint.h>

typedef struct {
    uintptr_t base_addr;
    uint32_t corb_size;
    uint32_t rirb_size;
} hda_controller_t;

void hda_init(hda_controller_t *controller);
void hda_send_cmd(hda_controller_t *controller, uint32_t codec_addr, uint32_t nid, uint32_t verb, uint32_t payload);

#endif
