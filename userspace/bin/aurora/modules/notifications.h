#ifndef AURORA_NOTIFICATIONS_H
#define AURORA_NOTIFICATIONS_H

#include "aurora_types.h"
#include "draw.h"

static inline void render_notification(uint32_t *pixels, uint32_t width, uint32_t height,
                                       const struct akaos_notification *note,
                                       const struct aurora_clock *clock) {
    /* Notification container */
    fill_rounded_rect(pixels, width, height, 0, 0, (int)width, (int)height, 12,
                      g_light_theme ? UINT32_C(0xf8fafc) : UINT32_C(0x172130),
                      g_light_theme ? UINT32_C(0xb9c5d3) : UINT32_C(0x50627b));

    /* App icon */
    draw_app_icon(pixels, width, height, 14, 14,
                  note->app_id[0] ? note->app_id : "system");

    /* Title */
    draw_ui_text(pixels, width, height, 54, 10, 1,
                 note->title[0] ? note->title : "akaOS Notification",
                 g_light_theme ? UINT32_C(0x203047) : UINT32_C(0xf3f6fb),
                 (int)width - 105);

    /* Timestamp */
    if (clock && clock->valid) {
        char stamp[8];
        snprintf(stamp, sizeof(stamp), "%02d:%02d", clock->hour, clock->minute);
        draw_ui_text(pixels, width, height, (int)width - 48, 10, 1, stamp,
                     UINT32_C(0x78879b), 42);
    }

    /* Body text */
    draw_ui_text(pixels, width, height, 54, 38, 1, note->body,
                 g_light_theme ? UINT32_C(0x4b5a6e) : UINT32_C(0xb9c5d5),
                 (int)width - 68);

    /* Close 'x' */
    draw_ui_text(pixels, width, height, (int)width - 22, 62, 1, "x",
                 UINT32_C(0x8795a8), 12);
}

#endif /* AURORA_NOTIFICATIONS_H */
