#ifndef AURORA_PANEL_H
#define AURORA_PANEL_H

#include "aurora_types.h"
#include "draw.h"

static inline uint32_t visible_window_count(const struct akaos_compositor_channel *channel,
                                             uint32_t limit) {
    if (!channel) return 0;
    uint32_t count = 0;
    for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES && count < limit; ++i) {
        const struct akaos_compositor_slot *slot = &channel->surfaces[i];
        if (slot->active &&
            !(slot->flags & (AKAOS_SURFACE_FLAG_DESKTOP | AKAOS_SURFACE_FLAG_PANEL))) {
            ++count;
        }
    }
    return count;
}

static inline uint32_t panel_window_state(const struct akaos_compositor_channel *channel) {
    if (!channel) return 0;
    uint32_t state = channel->active_surface_id * UINT32_C(16777619);
    for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
        const struct akaos_compositor_slot *slot = &channel->surfaces[i];
        if (!slot->active ||
            (slot->flags & (AKAOS_SURFACE_FLAG_DESKTOP | AKAOS_SURFACE_FLAG_PANEL))) continue;
        state ^= slot->surface_id + slot->flags * 31U + slot->z_order * 131U;
        state *= UINT32_C(16777619);
    }
    return state;
}

static inline uint32_t get_tray_width(const struct aurora_clock *clock,
                                      const struct aurora_capabilities *caps) {
    uint32_t icons = (caps ? (caps->net.available + caps->audio + caps->battery) : 1) * 32U;
    uint32_t clock_width = (clock && clock->valid) ? 116U : 40U;
    return icons + clock_width + 16U;
}

static inline void render_panel(uint32_t *pixels, uint32_t width, uint32_t height,
                                struct akaos_compositor_channel *channel,
                                const struct aurora_clock *clock,
                                const struct aurora_capabilities *caps) {
    /* Full-width dark glass rail with subtle lighting */
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t fade = (y * 6U) / (height ? height : 1U);
        uint32_t panel_r = g_light_theme ? 0xf4U - fade : 0x14U - fade;
        uint32_t panel_g = g_light_theme ? 0xf6U - fade : 0x1aU - fade;
        uint32_t panel_b = g_light_theme ? 0xf9U - fade : 0x24U - fade;
        uint32_t row_color = (panel_r << 16) | (panel_g << 8) | panel_b;
        if (y == 0) row_color = g_light_theme ? UINT32_C(0xbcc8d6) : UINT32_C(0x2f3d52);
        for (uint32_t x = 0; x < width; ++x) {
            pixels[y * width + x] = row_color;
        }
    }

    uint32_t tray_width = get_tray_width(clock, caps);
    uint32_t reserved = tray_width + 48U;
    uint32_t max_tasks = width > reserved + LAUNCHER_WIDTH
                             ? (width - reserved - LAUNCHER_WIDTH) / (TASK_WIDTH + TASK_GAP)
                             : 0U;
    uint32_t task_count = visible_window_count(channel, max_tasks);
    uint32_t dock_width = LAUNCHER_WIDTH +
                          (task_count ? 8U + task_count * (TASK_WIDTH + TASK_GAP) - TASK_GAP : 0U);
    uint32_t dock_x = width > dock_width ? (width - dock_width) / 2U : 4U;

    if (channel) {
        channel->shell_launcher_x = dock_x;
        channel->shell_task_count = task_count;
    }

    /* Launcher button container */
    fill_rounded_rect(pixels, width, height, (int)dock_x, 6,
                      (int)LAUNCHER_WIDTH, (int)height - 12, 9,
                      g_light_theme ? UINT32_C(0xe1e8f2) : UINT32_C(0x222e42),
                      g_light_theme ? UINT32_C(0xb4c2d4) : UINT32_C(0x3a4c68));

    /* Centered Aurora 4-quadrant emblem */
    uint32_t mark_x = dock_x + 11U;
    uint32_t mark_y = 15U;
    uint32_t mark_colors[4] = {UINT32_C(0x38bdf8), UINT32_C(0x4ade80),
                               UINT32_C(0xfbbf24), UINT32_C(0xc084fc)};
    for (uint32_t mark = 0; mark < 4; ++mark) {
        uint32_t mx = mark_x + (mark % 2U) * 11U;
        uint32_t my = mark_y + (mark / 2U) * 11U;
        for (uint32_t yy = 0; yy < 8; ++yy)
            for (uint32_t xx = 0; xx < 8; ++xx)
                pixels[(my + yy) * width + mx + xx] = mark_colors[mark];
    }

    /* Icon-only active task items in dock */
    if (channel) {
        uint32_t task_x = dock_x + LAUNCHER_WIDTH + 8U;
        uint32_t drawn = 0;
        for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
            const struct akaos_compositor_slot *s = &channel->surfaces[i];
            if (!s->active ||
                (s->flags & (AKAOS_SURFACE_FLAG_DESKTOP | AKAOS_SURFACE_FLAG_PANEL)) ||
                drawn >= task_count) continue;
            int focused = s->surface_id == channel->active_surface_id;
            fill_rounded_rect(pixels, width, height, (int)task_x, 8,
                              (int)TASK_WIDTH, (int)height - 16, 8,
                              focused ? (g_light_theme ? UINT32_C(0xd4e4fa) : UINT32_C(0x2d3e58))
                                      : (g_light_theme ? UINT32_C(0xe8edf4) : UINT32_C(0x1a2432)),
                              focused ? UINT32_C(0x60a5fa)
                                      : (g_light_theme ? UINT32_C(0xc2cdda) : UINT32_C(0x2d3b4e)));
            draw_app_icon(pixels, width, height, (int)task_x + 7, 10,
                          s->icon_name[0] ? s->icon_name :
                          (s->title[0] ? s->title : "Window"));
            if (focused) {
                fill_rounded_rect(pixels, width, height, (int)task_x + 15,
                                  (int)height - 5, 12, 3, 1,
                                  UINT32_C(0x38bdf8), UINT32_C(0x38bdf8));
            }
            task_x += TASK_WIDTH + TASK_GAP;
            ++drawn;
        }
    }

    /* Tray Area (Right-aligned) */
    if (width > tray_width + 16U) {
        uint32_t status_x = width - tray_width - 16U;
        fill_rounded_rect(pixels, width, height, (int)status_x, 7,
                          (int)tray_width, (int)height - 14, 9,
                          g_light_theme ? UINT32_C(0xe5eaf2) : UINT32_C(0x192230),
                          g_light_theme ? UINT32_C(0xbac5d3) : UINT32_C(0x2d3c52));
        uint32_t cursor_x = status_x + 16U;

        /* Network indicator */
        if (caps && caps->net.available) {
            draw_wifi_icon(pixels, width, height, (int)cursor_x, 28,
                           caps->net.connected);
            cursor_x += 32U;
        }

        /* Audio indicator */
        if (caps && caps->audio) {
            draw_audio_icon(pixels, width, height, (int)cursor_x, 24);
            cursor_x += 32U;
        }

        /* Digital Clock */
        if (clock && clock->valid) {
            if (cursor_x > status_x + 16U) {
                fill_rect(pixels, width, height, (int)cursor_x, 14, 1, 20,
                          g_light_theme ? UINT32_C(0xbac6d5) : UINT32_C(0x334358));
                cursor_x += 12U;
            }
            char time_text[8];
            char date_text[12];
            snprintf(time_text, sizeof(time_text), "%02d:%02d", clock->hour, clock->minute);
            snprintf(date_text, sizeof(date_text), "%02d/%02d/%02d",
                     clock->month, clock->day, clock->year % 100);
            draw_ui_text(pixels, width, height, (int)cursor_x, 6, 1,
                         time_text,
                         g_light_theme ? UINT32_C(0x1e2c3e) : UINT32_C(0xf1f5f9), 58);
            draw_ui_text(pixels, width, height, (int)cursor_x, 24, 1,
                         date_text,
                         g_light_theme ? UINT32_C(0x5a6a80) : UINT32_C(0x94a3b8), 104);
        }
    }
}

#endif /* AURORA_PANEL_H */
