/*
 * akaOS XSCore Lumen Wayland Compositor - Input Dispatcher & Hardware Cursor
 */

#include "input.h"
#include <xscore/heap.h>
#include <xscore/serial.h>
#include <xscore/string.h>

#define kmalloc(sz) xscore_heap_allocate(sz)
#define kfree(ptr)  xscore_heap_free(ptr)

int lumen_input_init(struct lumen_input_seat *seat, struct xscore_drm_device *drm) {
    if (!seat) return -1;
    memset(seat, 0, sizeof(*seat));

    seat->seat_id = 1U;
    seat->drm = drm;

    seat->cursor.width = 32U;
    seat->cursor.height = 32U;
    seat->cursor.hotspot_x = 0;
    seat->cursor.hotspot_y = 0;
    seat->cursor.visible = 1U;
    seat->cursor.pixels = (uint32_t *)kmalloc(32U * 32U * sizeof(uint32_t));
    if (seat->cursor.pixels) {
        memset(seat->cursor.pixels, 0xFF, 32U * 32U * sizeof(uint32_t));
    }

    xscore_serial_write("[LUMEN] Wayland Seat & Hardware Cursor Plane initialized\n");
    return 0;
}

void lumen_input_destroy(struct lumen_input_seat *seat) {
    if (!seat) return;
    if (seat->cursor.pixels) {
        kfree(seat->cursor.pixels);
        seat->cursor.pixels = NULL;
    }
}

void lumen_input_pointer_motion(struct lumen_input_seat *seat, int32_t x, int32_t y) {
    if (!seat) return;

    seat->cursor.x = x;
    seat->cursor.y = y;

    /* If DRM hardware cursor plane is supported, execute atomic cursor move */
    if (seat->drm) {
        struct drm_atomic_state state;
        memset(&state, 0, sizeof(state));
        state.flags = 0x02; /* Cursor plane update */
        xscore_drm_atomic_commit(seat->drm, &state);
    }
}

void lumen_input_pointer_button(struct lumen_input_seat *seat, uint32_t button, uint32_t state) {
    if (!seat) return;

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        seat->mouse_buttons |= (1U << button);
    } else {
        seat->mouse_buttons &= ~(1U << button);
    }
}

void lumen_input_keyboard_key(struct lumen_input_seat *seat, uint32_t key, uint32_t state) {
    if (!seat) return;
    (void)key;
    (void)state;
}
