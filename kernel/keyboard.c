/* ============================================================
 * akaOS — PS/2 Keyboard Driver Implementation
 * ============================================================
 * Handles IRQ1 interrupts, translates scancodes to ASCII,
 * supports extended scancodes (arrow keys, etc.).
 */

#include "keyboard.h"
#include "idt.h"
#include "io.h"
#include "vga.h"

/* Keyboard buffer */
#define KB_BUFFER_SIZE 256
static char kb_buffer[KB_BUFFER_SIZE];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;

/* Modifier state */
static int shift_pressed = 0;
static int extended_key  = 0;

/* US QWERTY scancode → ASCII lookup (set 1) */
static const char sc_to_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0,
    0, 0, 0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char sc_to_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?',0,
    '*', 0, ' ', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0,
    0, 0, 0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static void kb_buffer_push(char c) {
    int next = (kb_head + 1) % KB_BUFFER_SIZE;
    if (next != kb_tail) {
        kb_buffer[kb_head] = c;
        kb_head = next;
    }
}

void (*keyboard_event_hook)(int scancode, int pressed) = 0;

static void keyboard_irq_handler(struct regs *r) {
    (void)r;
    uint8_t status = inb(0x64);
    if (!(status & 1)) return; /* No data available */

    uint8_t scancode = inb(0x60);
    if (status & 0x20) return; /* Actually mouse data! Discard to prevent lockup */

    /* Extended scancode prefix */
    if (scancode == 0xE0) {
        extended_key = 1;
        return;
    }

    /* Dispatch raw scancode hook if registered (for DOOM) */
    if (keyboard_event_hook) {
        int pressed = (scancode & 0x80) ? 0 : 1;
        int code = scancode & 0x7F;
        if (extended_key) code |= 0x100; /* Mark extended keys clearly */
        keyboard_event_hook(code, pressed);
    }

    /* Handle extended keys (arrow keys, etc.) */
    if (extended_key) {
        extended_key = 0;
        if (scancode & 0x80) return; /* Release of extended key */

        switch (scancode) {
            case 0x48: kb_buffer_push(KEY_UP);     return;
            case 0x50: kb_buffer_push(KEY_DOWN);   return;
            case 0x4B: kb_buffer_push(KEY_LEFT);   return;
            case 0x4D: kb_buffer_push(KEY_RIGHT);  return;
            case 0x47: kb_buffer_push(KEY_HOME);   return;
            case 0x4F: kb_buffer_push(KEY_END);    return;
            case 0x53: kb_buffer_push(KEY_DELETE);  return;
            case 0x49: kb_buffer_push(KEY_PGUP);   return;
            case 0x51: kb_buffer_push(KEY_PGDN);   return;
        }
        return;
    }

    /* Key release */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == 42 || released == 54)
            shift_pressed = 0;
        return;
    }

    /* Shift press */
    if (scancode == 42 || scancode == 54) {
        shift_pressed = 1;
        return;
    }

    /* Translate to ASCII */
    char c = shift_pressed ? sc_to_ascii_shift[scancode] : sc_to_ascii[scancode];
    if (c != 0)
        kb_buffer_push(c);
}

static void kb_wait_write(void) {
    int timeout = 100000;
    while (timeout-- && (inb(0x64) & 2));
}

static void kb_wait_read(void) {
    int timeout = 100000;
    while (timeout-- && !(inb(0x64) & 1));
}

void keyboard_init(void) {
    irq_install_handler(1, keyboard_irq_handler);

    /* Disable First PS/2 port to prevent data races during config */
    kb_wait_write();
    outb(0x64, 0xAD);

    /* Drain buffer with a timeout */
    int drain_timeout = 100000;
    while ((inb(0x64) & 1) && drain_timeout--) {
        inb(0x60);
    }

    /* Read i8042 Configuration Byte */
    kb_wait_write();
    outb(0x64, 0x20);
    
    kb_wait_read();
    uint8_t status = 0x47; /* Fallback safe mask */
    if (inb(0x64) & 1) {
        status = inb(0x60);
    }

    /* Enable IRQ1 (bit 0), enable First PS/2 port (clear bit 4),
     * and enable Scancode Set 1 translation (bit 6). */
    status |= 1;
    status &= ~0x10;
    status |= 0x40;

    /* Write i8042 Configuration Byte */
    kb_wait_write();
    outb(0x64, 0x60);
    kb_wait_write();
    outb(0x60, status);

    /* Re-enable First PS/2 port */
    kb_wait_write();
    outb(0x64, 0xAE);
}

int keyboard_has_char(void) {
    return kb_head != kb_tail;
}

char keyboard_getchar(void) {
    while (kb_head == kb_tail) {
        arch_halt();
    }
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    return c;
}
