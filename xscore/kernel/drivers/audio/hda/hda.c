
#include "hda.h"
#include <xscore/memory.h>
#include <xscore/io.h>

// HDA Register Offsets
#define HDA_REG_GCTL      0x08
#define HDA_REG_CORBSIZE  0x42
#define HDA_REG_CORB      0x4C
#define HDA_REG_CORBRP    0x4A
#define HDA_REG_CORBWP    0x48
#define HDA_REG_RIRBSIZE  0x52
#define HDA_REG_RIRB      0x5C
#define HDA_REG_RIRBRP    0x5A
#define HDA_REG_RIRBWP    0x58

void hda_init(hda_controller_t *controller) {
    // 1. Reset Controller
    mmio_write32(controller->base_addr + HDA_REG_GCTL, 0);
    // Wait for reset...

    // 2. Initialize CORB (Command Outbound Ring Buffer)
    mmio_write32(controller->base_addr + HDA_REG_CORBSIZE, 0x02); // 2 entries
    mmio_write32(controller->base_addr + HDA_REG_CORB, (uint32_t)0); // Set addr
    mmio_write32(controller->base_addr + HDA_REG_CORBWP, 0); // WP
    mmio_write32(controller->base_addr + HDA_REG_CORBRP, 0x01); // RP (reset)
    
    // 3. Initialize RIRB (Response Inbound Ring Buffer)
    mmio_write32(controller->base_addr + HDA_REG_RIRBSIZE, 0x02); // 2 entries
    mmio_write32(controller->base_addr + HDA_REG_RIRB, (uint32_t)0); // Set addr
    mmio_write32(controller->base_addr + HDA_REG_RIRBWP, 0); // WP
}

void hda_send_cmd(hda_controller_t *controller, uint32_t codec_addr, uint32_t nid, uint32_t verb, uint32_t payload) {
    uint32_t cmd = (codec_addr << 28) | (nid << 20) | (verb << 8) | payload;
    
    // Write cmd to CORB buffer location (simplified for this step)
    // ...
    uint16_t wp = mmio_read16(controller->base_addr + HDA_REG_CORBWP);
    mmio_write32(controller->base_addr + 0x60 + (wp * 4), cmd); // Assuming base offset
    mmio_write16(controller->base_addr + HDA_REG_CORBWP, (wp + 1) % 2);
}
