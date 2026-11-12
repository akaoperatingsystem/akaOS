#ifndef AKAOS_INPUT_H
#define AKAOS_INPUT_H

#include <stdint.h>

/* Stable, timestamp-free input event ABI.  Values intentionally follow the
 * common EV_* convention so device drivers can forward native events without
 * translating their type/code/value payload. */
#define AKAOS_EV_SYN  UINT16_C(0x00)
#define AKAOS_EV_KEY  UINT16_C(0x01)
#define AKAOS_EV_REL  UINT16_C(0x02)
#define AKAOS_EV_ABS  UINT16_C(0x03)
#define AKAOS_EV_TEXT UINT16_C(0x04)

#define AKAOS_SYN_REPORT UINT16_C(0)
#define AKAOS_KEY_ESC    UINT16_C(1)
#define AKAOS_REL_X      UINT16_C(0)
#define AKAOS_REL_Y      UINT16_C(1)
#define AKAOS_REL_WHEEL  UINT16_C(8)
#define AKAOS_ABS_X      UINT16_C(0)
#define AKAOS_ABS_Y      UINT16_C(1)
#define AKAOS_BTN_LEFT   UINT16_C(272)
#define AKAOS_BTN_RIGHT  UINT16_C(273)
#define AKAOS_BTN_MIDDLE UINT16_C(274)

#define AKAOS_REL_HWHEEL UINT16_C(6)
#define AKAOS_BTN_SIDE   UINT16_C(275)
#define AKAOS_BTN_EXTRA  UINT16_C(276)

/* Mouse Acceleration Profiles */
#define AKAOS_MOUSE_ACCEL_FIXED    0
#define AKAOS_MOUSE_ACCEL_LINEAR   1
#define AKAOS_MOUSE_ACCEL_ADAPTIVE 2


#define AKAOS_INPUT_EVENT_PATH "/dev/input/event0"

struct akaos_input_event {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

#endif
