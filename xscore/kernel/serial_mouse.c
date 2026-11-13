/*
 * Serial Mouse Driver for XSCore
 * Supports Microsoft 2-Button / 3-Button / Wheel and Mouse Systems protocols over UART COM ports
 */

#include <xscore/mouse.h>
#include <xscore/input.h>
#include <xscore/io.h>
#include <akaos/input.h>
#include <stddef.h>
#include <stdint.h>

/* Standard COM port bases */
#define COM1_PORT 0x3F8
#define COM2_PORT 0x2F8

static uint8_t s_serial_mouse_packet[5];
static uint32_t s_serial_packet_idx = 0;
static uint8_t s_serial_mouse_buttons = 0;
static uint16_t s_active_com_port = COM1_PORT;

void xscore_serial_mouse_input_byte(uint8_t byte) {
    /* Microsoft Serial Mouse Protocol (1200 bps, 7 data bits, 1 stop bit)
     * Byte 0: 01-Sync Y7-Y6 X7-X6 L-R
     * Byte 1: 00 X5 X4 X3 X2 X1 X0
     * Byte 2: 00 Y5 Y4 Y3 Y2 Y1 Y0
     * Byte 3 (Optional 3-button/wheel): 00 M 0 Z3 Z2 Z1 Z0
     */
    if (byte & 0x40) {
        /* Sync bit indicates start of packet */
        s_serial_packet_idx = 0;
        s_serial_mouse_packet[s_serial_packet_idx++] = byte;
        return;
    }

    if (s_serial_packet_idx == 0) return; /* Discard bytes before sync */

    s_serial_mouse_packet[s_serial_packet_idx++] = byte;

    if (s_serial_packet_idx >= 3) {
        uint8_t b0 = s_serial_mouse_packet[0];
        uint8_t b1 = s_serial_mouse_packet[1];
        uint8_t b2 = s_serial_mouse_packet[2];

        int32_t dx = (int32_t)(((b0 & 0x03) << 6) | (b1 & 0x3F));
        int32_t dy = (int32_t)(((b0 & 0x0C) << 4) | (b2 & 0x3F));

        /* Sign-extend 8-bit deltas */
        if (dx >= 128) dx -= 256;
        if (dy >= 128) dy -= 256;

        uint8_t buttons = 0;
        if (b0 & 0x20) buttons |= 1; /* Left */
        if (b0 & 0x10) buttons |= 2; /* Right */

        /* Check for 4th byte (Logitech / MS 3-button extension) */
        if (s_serial_packet_idx == 4) {
            uint8_t b3 = s_serial_mouse_packet[3];
            if (b3 & 0x20) buttons |= 4; /* Middle button */
            int8_t wheel = (int8_t)(b3 & 0x0F);
            if (wheel & 0x08) wheel |= 0xF0;
            if (wheel != 0) {
                xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_WHEEL, (int32_t)wheel);
            }
            s_serial_packet_idx = 0;
        } else {
            /* 3-byte packet received */
            s_serial_packet_idx = 0;
        }

        if (dx != 0) xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_X, dx);
        if (dy != 0) xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_Y, dy);

        if ((buttons ^ s_serial_mouse_buttons) & 1)
            xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_LEFT, (buttons & 1) != 0);
        if ((buttons ^ s_serial_mouse_buttons) & 2)
            xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_RIGHT, (buttons & 2) != 0);
        if ((buttons ^ s_serial_mouse_buttons) & 4)
            xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_MIDDLE, (buttons & 4) != 0);

        s_serial_mouse_buttons = buttons;
        xscore_input_publish(AKAOS_EV_SYN, AKAOS_SYN_REPORT, 0);
    }
}

static int serial_mouse_initialize(void) {
    s_serial_packet_idx = 0;
    s_serial_mouse_buttons = 0;

    /* Configure COM2 (0x2F8) at 1200 baud, 7-N-1 with DTR/RTS set */
    uint16_t port = COM2_PORT;
    s_active_com_port = port;

    port_out8(port + 1, 0x00); /* Disable interrupts */
    port_out8(port + 3, 0x80); /* Enable DLAB (set baud rate divisor) */
    port_out8(port + 0, 0x60); /* Divisor 96 (low byte) -> 1200 baud */
    port_out8(port + 1, 0x00); /* Divisor (high byte) */
    port_out8(port + 3, 0x02); /* 7 data bits, no parity, 1 stop bit (7-N-1) */
    port_out8(port + 2, 0xC7); /* Enable FIFO, clear them, with 14-byte threshold */
    port_out8(port + 4, 0x0B); /* IRQs enabled, RTS/DSR set */

    return 1;
}

static int serial_mouse_read_event(int *dx, int *dy, int *buttons) {
    (void)dx; (void)dy; (void)buttons;
    return 0;
}

static struct xscore_mouse_driver s_serial_driver = {
    .type = MOUSE_TYPE_SERIAL,
    .initialize = serial_mouse_initialize,
    .read_event = serial_mouse_read_event
};

void xscore_serial_mouse_register(void) {
    xscore_mouse_register_driver(&s_serial_driver);
}
