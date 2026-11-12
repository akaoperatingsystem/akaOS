
#include <xscore/mouse.h>
#include <xscore/ps2.h>
#include <xscore/io.h>
#include <xscore/input.h>
#include <akaos/input.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t ps2_mouse_packet[4];
static uint32_t ps2_mouse_packet_index = 0;
static uint8_t ps2_mouse_buttons = 0;
static uint8_t ps2_mouse_device_id = 0;

static int ps2_mouse_write(uint8_t value) {
    uint8_t response = 0;
    if (!xscore_ps2_wait_input_clear()) return 0;
    port_out8(0x64, 0xd4);
    if (!xscore_ps2_wait_input_clear()) return 0;
    port_out8(0x60, value);
    return xscore_ps2_wait_output(&response) && response == 0xfa;
}

void ps2_mouse_input_byte(uint8_t value) {
    if (ps2_mouse_packet_index == 0 && (value & 0x08) == 0) return;
    ps2_mouse_packet[ps2_mouse_packet_index++] = value;
    
    uint32_t packet_size = (ps2_mouse_device_id >= 3) ? 4 : 3;
    if (ps2_mouse_packet_index != packet_size) return;
    ps2_mouse_packet_index = 0;

    uint8_t flags = ps2_mouse_packet[0];
    if ((flags & 0xc0) != 0) return;
    int32_t relative_x = (int32_t)(int8_t)ps2_mouse_packet[1];
    int32_t relative_y = -(int32_t)(int8_t)ps2_mouse_packet[2];
    uint8_t buttons = flags & 7;

    if (relative_x != 0) xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_X, relative_x);
    if (relative_y != 0) xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_Y, relative_y);
    if ((buttons ^ ps2_mouse_buttons) & 1) xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_LEFT, (buttons & 1) != 0);
    if ((buttons ^ ps2_mouse_buttons) & 2) xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_RIGHT, (buttons & 2) != 0);
    if ((buttons ^ ps2_mouse_buttons) & 4) xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_MIDDLE, (buttons & 4) != 0);
    
    /* IntelliMouse wheel and extra buttons */
    if (ps2_mouse_device_id >= 3) {
        int8_t z = (int8_t)ps2_mouse_packet[3];
        if (ps2_mouse_device_id == 3) {
            if (z != 0) xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_WHEEL, z);
        } else if (ps2_mouse_device_id == 4) {
            int8_t wheel = z & 0x0F;
            if (wheel & 0x08) wheel |= 0xF0; /* sign extend 4-bit */
            if (wheel != 0) xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_WHEEL, wheel);
            
            xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_SIDE, (z & 0x10) != 0);
            xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_EXTRA, (z & 0x20) != 0);
        }
    }

    ps2_mouse_buttons = buttons;
    xscore_input_publish(AKAOS_EV_SYN, AKAOS_SYN_REPORT, 0);
}

static int ps2_mouse_initialize(void) {
    ps2_mouse_packet_index = 0;
    ps2_mouse_buttons = 0;
    ps2_mouse_device_id = 0;
    xscore_ps2_flush_output();
    if (!xscore_ps2_wait_input_clear()) return 0;
    port_out8(0x64, 0xa8);
    if (!xscore_ps2_wait_input_clear()) return 0;
    port_out8(0x64, 0x20);
    uint8_t config = 0;
    if (!xscore_ps2_wait_output(&config)) return 0;
    config = (uint8_t)((config | 0x03U) & (uint8_t)~0x30U);
    if (!xscore_ps2_wait_input_clear()) return 0;
    port_out8(0x64, 0x60);
    if (!xscore_ps2_wait_input_clear()) return 0;
    port_out8(0x60, config);
    xscore_ps2_flush_output();
    
    if (!ps2_mouse_write(0xf6)) return 0;
    
    /* IntelliMouse detection sequence: sample rates 200 -> 100 -> 80 */
    ps2_mouse_write(0xf3); ps2_mouse_write(200);
    ps2_mouse_write(0xf3); ps2_mouse_write(100);
    ps2_mouse_write(0xf3); ps2_mouse_write(80);
    
    /* Read Device ID */
    if (ps2_mouse_write(0xf2)) {
        uint8_t id = 0;
        if (xscore_ps2_wait_output(&id)) {
            ps2_mouse_device_id = id;
            if (id == 3) {
                /* Try 5-button IntelliMouse detection: 200 -> 200 -> 80 */
                ps2_mouse_write(0xf3); ps2_mouse_write(200);
                ps2_mouse_write(0xf3); ps2_mouse_write(200);
                ps2_mouse_write(0xf3); ps2_mouse_write(80);
                if (ps2_mouse_write(0xf2)) {
                    uint8_t id5 = 0;
                    if (xscore_ps2_wait_output(&id5) && id5 == 4) {
                        ps2_mouse_device_id = 4;
                    }
                }
            }
        }
    }
    
    return ps2_mouse_write(0xf4);
}

static int ps2_mouse_read_event(int *dx, int *dy, int *buttons) {
    (void)dx; (void)dy; (void)buttons;
    return 0; 
}

static struct xscore_mouse_driver ps2_driver = {
    .type = MOUSE_TYPE_PS2,
    .initialize = ps2_mouse_initialize,
    .read_event = ps2_mouse_read_event
};

void ps2_mouse_register(void) {
    xscore_mouse_register_driver(&ps2_driver);
}
