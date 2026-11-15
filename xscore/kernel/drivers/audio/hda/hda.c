
#include "hda.h"
#include <xscore/memory.h>
#include <xscore/io.h>

// BDL Entry
typedef struct {
    uint64_t addr;
    uint32_t len;
    uint32_t flags;
} hda_bdl_entry_t;

// HDA Register Offsets
#define HDA_REG_GCTL      0x08
#define HDA_REG_CORBSIZE  0x42
#define HDA_REG_CORB      0x4C

void hda_init(hda_controller_t *controller) {
    // 1. Reset Controller
    mmio_write32(controller->base_addr + HDA_REG_GCTL, 0);
    // 2. Initialize BDL ...
}

void hda_send_cmd(hda_controller_t *controller, uint32_t codec_addr, uint32_t nid, uint32_t verb, uint32_t payload) {
    uint32_t cmd = (codec_addr << 28) | (nid << 20) | (verb << 8) | payload;
    // ... CORB write ...
}
