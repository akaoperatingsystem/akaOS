/* ============================================================
 * akaOS — PS/2 Mouse Driver
 * ============================================================ */
#include "mouse.h"
#include "idt.h"
#include "io.h"

static int mouse_x = 512, mouse_y = 384;
static int mouse_btns = 0;
static int bound_w = 1024, bound_h = 768;
static int mouse_cycle = 0;
static int8_t mouse_bytes[3];

static void mouse_wait_write(void) {
    int t = 100000;
    while (t-- > 0) { if (!(inb(0x64) & 2)) return; }
}
static void mouse_wait_read(void) {
    int t = 100000;
    while (t-- > 0) { if (inb(0x64) & 1) return; }
}
static void mouse_write(uint8_t val) {
    mouse_wait_write();
    outb(0x64, 0xD4);  /* Tell controller: next byte goes to mouse */
    mouse_wait_write();
    outb(0x60, val);
}

static void mouse_irq_handler(struct regs *r) {
    (void)r;
    uint8_t status = inb(0x64);
    if (!(status & 1)) return; /* No data available */

    int8_t data = (int8_t)inb(0x60);
    if (!(status & 0x20)) return; /* Not mouse data (keyboard) — discarded to prevent lockup */

    switch (mouse_cycle) {
    case 0:
        mouse_bytes[0] = data;
        if (data & 0x08) mouse_cycle = 1; /* Valid first byte has bit3 set */
        break;
    case 1:
        mouse_bytes[1] = data;
        mouse_cycle = 2;
        break;
    case 2:
        mouse_bytes[2] = data;
        mouse_cycle = 0;
        /* Process packet */
        mouse_btns = mouse_bytes[0] & 0x07;
        mouse_x += mouse_bytes[1];
        mouse_y -= mouse_bytes[2]; /* Y is inverted */
        /* Clamp */
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= bound_w) mouse_x = bound_w - 1;
        if (mouse_y >= bound_h) mouse_y = bound_h - 1;
        break;
    }
}

void mouse_init(void) {
    /* Disable interrupts during PS/2 config */
    asm volatile("cli");

    /* Enable mouse on PS/2 controller */
    mouse_wait_write();
    outb(0x64, 0xA8);

    /* Get compaq status */
    mouse_wait_write();
    outb(0x64, 0x20);
    mouse_wait_read();
    uint8_t status = inb(0x60);

    status |= 2;       /* Enable IRQ12 */
    status &= ~0x20;   /* Clear disable mouse clock */

    /* Set compaq status */
    mouse_wait_write();
    outb(0x64, 0x60);
    mouse_wait_write();
    outb(0x60, status);

    /* Use default settings */
    mouse_write(0xF6);
    mouse_wait_read();
    inb(0x60); /* ACK */

    /* Enable data reporting */
    mouse_write(0xF4);
    mouse_wait_read();
    inb(0x60); /* ACK */

    /* Drain buffer completely */
    while (inb(0x64) & 1) {
        inb(0x60);
    }

    /* Install handler and re-enable interrupts */
    irq_install_handler(12, mouse_irq_handler);
    asm volatile("sti");
}

int mouse_get_x(void)       { return mouse_x; }
int mouse_get_y(void)       { return mouse_y; }
int mouse_get_buttons(void) { return mouse_btns; }
void mouse_set_bounds(int w, int h) { bound_w = w; bound_h = h; }
