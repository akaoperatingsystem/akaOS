#include "doomgeneric.h"
#include "doomkeys.h"

/* Undef macros that conflict with keyboard.h */
#undef KEY_HOME
#undef KEY_END
#undef KEY_PGUP
#undef KEY_PGDN

#include "fb.h"
#include "keyboard.h"
#include "time.h"
#include "string.h"

/* ===== DG_ScreenBuffer (required by doomgeneric) ===== */
/* DG_ScreenBuffer is declared extern in doomgeneric.h and defined in doomgeneric.c */
static pixel_t screen_buf[DOOMGENERIC_RESX * DOOMGENERIC_RESY];

/* ===== Key event queue ===== */
#define DOOM_KEY_QUEUE_SIZE 64
static struct { int scancode; int pressed; } doom_key_queue[DOOM_KEY_QUEUE_SIZE];
static volatile int dkq_head = 0;
static volatile int dkq_tail = 0;

extern void (*keyboard_event_hook)(int scancode, int pressed);
extern int active_window_focus;

static void doom_keyboard_hook(int scancode, int pressed) {
    if (active_window_focus != 5) return;
    int next = (dkq_head + 1) % DOOM_KEY_QUEUE_SIZE;
    if (next != dkq_tail) {
        doom_key_queue[dkq_head].scancode = scancode;
        doom_key_queue[dkq_head].pressed = pressed;
        dkq_head = next;
    }
}

static void enable_sse(void) {
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1UL << 2);
    cr0 |=  (1UL << 1);
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
    uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1UL << 9) | (1UL << 10);
    asm volatile("mov %0, %%cr4" :: "r"(cr4));
}

/* ===== DG_Init — called by doomgeneric_Create() ===== */
void DG_Init(void) {
    enable_sse();
    DG_ScreenBuffer = screen_buf;
    keyboard_event_hook = doom_keyboard_hook;
}

/* ===== Scancode → DOOM key translation ===== */
static unsigned char scancode_to_doom(int scancode) {
    switch (scancode) {
        case 0x1C: return KEY_ENTER;
        case 0x01: return KEY_ESCAPE;
        case 0x4B | 0x100: return KEY_LEFTARROW;
        case 0x4D | 0x100: return KEY_RIGHTARROW;
        case 0x48 | 0x100: return KEY_UPARROW;
        case 0x50 | 0x100: return KEY_DOWNARROW;
        case 0x1D: return KEY_RCTRL;
        case 0x1D | 0x100: return KEY_RCTRL;
        case 0x39: return ' ';
        case 0x2A: case 0x36: return KEY_RSHIFT;
        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;
        case 0x38: return KEY_LALT;
        case 0x21: return 'f';
        case 0x02: return '1'; case 0x03: return '2';
        case 0x04: return '3'; case 0x05: return '4';
        case 0x06: return '5'; case 0x07: return '6';
        case 0x08: return '7'; case 0x09: return '8';
        case 0x0A: return '9'; case 0x0B: return '0';
        case 0x10: return 'q'; case 0x11: return 'w';
        case 0x12: return 'e'; case 0x13: return 'r';
        case 0x14: return 't'; case 0x15: return 'y';
        case 0x16: return 'u'; case 0x17: return 'i';
        case 0x18: return 'o'; case 0x19: return 'p';
        case 0x1E: return 'a'; case 0x1F: return 's';
        case 0x20: return 'd'; case 0x22: return 'g';
        case 0x23: return 'h'; case 0x24: return 'j';
        case 0x25: return 'k'; case 0x26: return 'l';
        case 0x2C: return 'z'; case 0x2D: return 'x';
        case 0x2E: return 'c'; case 0x2F: return 'v';
        case 0x30: return 'b'; case 0x31: return 'n';
        case 0x32: return 'm';
        case 0x33: return ','; case 0x34: return '.';
    }
    return 0;
}

/* ===== DG_GetKey — called by i_input.c ===== */
int DG_GetKey(int *pressed, unsigned char *doomKey) {
    if (dkq_head == dkq_tail) return 0;
    int scancode = doom_key_queue[dkq_tail].scancode;
    *pressed = doom_key_queue[dkq_tail].pressed;
    dkq_tail = (dkq_tail + 1) % DOOM_KEY_QUEUE_SIZE;
    *doomKey = scancode_to_doom(scancode);
    if (*doomKey == 0) return 0;
    return 1;
}

/* ===== DG_DrawFrame (now handled asynchronously by gui.c) ===== */
void DG_DrawFrame(void) {
    /* Nothing! We draw DG_ScreenBuffer directly in draw_doom_window */
}

/* ===== DG_GetTicksMs — called by i_timer.c ===== */
uint32_t DG_GetTicksMs(void) {
    return timer_get_ticks();
}

/* ===== DG_SleepMs — called by i_timer.c ===== */
void DG_SleepMs(uint32_t ms) {
    uint32_t target = timer_get_ticks() + ms;
    while (timer_get_ticks() < target) {
        asm volatile("hlt");
    }
}

/* ===== DG_SetWindowTitle — no-op ===== */
void DG_SetWindowTitle(const char *title) {
    (void)title;
}
