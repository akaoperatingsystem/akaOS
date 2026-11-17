/*
 * akaOS XSCore Lumen Wayland Compositor - Input Dispatcher & Hardware Cursor
 */

#ifndef LUMEN_WAYLAND_INPUT_H
#define LUMEN_WAYLAND_INPUT_H

#include "protocol.h"
#include <xscore/drm_kms.h>
#include <stdint.h>

#define WL_POINTER_BUTTON_STATE_RELEASED 0U
#define WL_POINTER_BUTTON_STATE_PRESSED  1U

#define WL_KEYBOARD_KEY_STATE_RELEASED   0U
#define WL_KEYBOARD_KEY_STATE_PRESSED    1U

struct lumen_cursor {
    int32_t x;
    int32_t y;
    int32_t hotspot_x;
    int32_t hotspot_y;
    uint32_t width;
    uint32_t height;
    uint32_t *pixels;
    uint32_t visible;
    uint32_t drm_cursor_plane_id;
};

struct lumen_input_seat {
    uint32_t seat_id;
    struct lumen_cursor cursor;
    struct lumen_surface *pointer_focus;
    struct lumen_surface *keyboard_focus;
    uint32_t mouse_buttons;
    uint32_t keyboard_modifiers;
    struct xscore_drm_device *drm;
};

int lumen_input_init(struct lumen_input_seat *seat, struct xscore_drm_device *drm);
void lumen_input_destroy(struct lumen_input_seat *seat);
void lumen_input_pointer_motion(struct lumen_input_seat *seat, int32_t x, int32_t y);
void lumen_input_pointer_button(struct lumen_input_seat *seat, uint32_t button, uint32_t state);
void lumen_input_keyboard_key(struct lumen_input_seat *seat, uint32_t key, uint32_t state);

#endif /* LUMEN_WAYLAND_INPUT_H */
