/*
 * akaOS Aurora Desktop - Modern Glassmorphic Floating Dock & Status Pill
 *
 * Professional 2026 UI architecture:
 * - Floating centered glassmorphic dock with rounded corners and ambient shadow
 * - Running task indicators with active cyan glow pill
 * - Floating right-aligned system status pill with network, audio, and clock
 */

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
    uint32_t clock_width = (clock && clock->valid) ? 120U : 44U;
    return icons + clock_width + 20U;
}

static inline void render_panel(uint32_t *pixels, uint32_t width, uint32_t height,
                                struct akaos_compositor_channel *channel,
                                const struct aurora_clock *clock,
                                const struct aurora_capabilities *caps) {
    /* 1. Clear surface to 100% transparent so wallpaper shines through around floating dock */
    size_t total_pixels = (size_t)width * height;
    memset(pixels, 0, total_pixels * sizeof(uint32_t));

    /* 2. Compute Floating Centered Dock Metrics */
    uint32_t tray_width = get_tray_width(clock, caps);
    uint32_t reserved = tray_width + 48U;
    uint32_t max_tasks = width > reserved + LAUNCHER_WIDTH
                             ? (width - reserved - LAUNCHER_WIDTH) / (TASK_WIDTH + TASK_GAP)
                             : 0U;
    uint32_t task_count = visible_window_count(channel, max_tasks);
    uint32_t dock_content_width = LAUNCHER_WIDTH +
                                  (task_count ? 10U + task_count * (TASK_WIDTH + TASK_GAP) - TASK_GAP : 0U);
    uint32_t dock_padding = 10U;
    uint32_t dock_total_w = dock_content_width + dock_padding * 2U;
    uint32_t dock_x = width > dock_total_w ? (width - dock_total_w) / 2U : 8U;
    uint32_t dock_y = 4U;
    uint32_t dock_h = height > 8U ? height - 8U : height;

    if (channel) {
        channel->shell_launcher_x = dock_x + dock_padding;
        channel->shell_task_count = task_count;
    }

    /* 3. Render Modern Floating Glass Dock */
    /* Ambient Drop Shadow */
    fill_rounded_rect(pixels, width, height, (int)dock_x, (int)dock_y + 2,
                      (int)dock_total_w, (int)dock_h, 14,
                      UINT32_C(0x40000000), UINT32_C(0x20000000));

    /* Frosted Glass Base: Midnight acrylic with crisp specular border */
    uint32_t dock_fill = g_light_theme ? UINT32_C(0xE6F8FAFC) : UINT32_C(0xE60F172A);
    uint32_t dock_border = g_light_theme ? UINT32_C(0x50000000) : UINT32_C(0x40FFFFFF);
    fill_rounded_rect(pixels, width, height, (int)dock_x, (int)dock_y,
                      (int)dock_total_w, (int)dock_h, 14,
                      dock_fill, dock_border);

    /* 4. Launcher Emblem Button */
    uint32_t launcher_btn_x = dock_x + dock_padding;
    uint32_t launcher_btn_y = dock_y + 4U;
    uint32_t launcher_btn_w = LAUNCHER_WIDTH;
    uint32_t launcher_btn_h = dock_h - 8U;

    fill_rounded_rect(pixels, width, height, (int)launcher_btn_x, (int)launcher_btn_y,
                      (int)launcher_btn_w, (int)launcher_btn_h, 10,
                      g_light_theme ? UINT32_C(0x2038BDF8) : UINT32_C(0x2838BDF8),
                      g_light_theme ? UINT32_C(0x6038BDF8) : UINT32_C(0x8038BDF8));

    /* Centered Modern Aurora 4-quadrant emblem */
    uint32_t mark_x = launcher_btn_x + 11U;
    uint32_t mark_y = launcher_btn_y + 7U;
    uint32_t mark_colors[4] = {UINT32_C(0xFF38BDF8), UINT32_C(0xFF34D399),
                               UINT32_C(0xFFFBBF24), UINT32_C(0xFFA855F7)};
    for (uint32_t mark = 0; mark < 4; ++mark) {
        uint32_t mx = mark_x + (mark % 2U) * 11U;
        uint32_t my = mark_y + (mark / 2U) * 11U;
        fill_rounded_rect(pixels, width, height, (int)mx, (int)my, 8, 8, 2,
                          mark_colors[mark], mark_colors[mark]);
    }

    /* 5. Icon-Only Active Task Items in Dock */
    if (channel) {
        uint32_t task_x = launcher_btn_x + LAUNCHER_WIDTH + 10U;
        uint32_t drawn = 0;
        for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
            const struct akaos_compositor_slot *s = &channel->surfaces[i];
            if (!s->active ||
                (s->flags & (AKAOS_SURFACE_FLAG_DESKTOP | AKAOS_SURFACE_FLAG_PANEL)) ||
                drawn >= task_count) continue;

            int focused = (s->surface_id == channel->active_surface_id);
            uint32_t task_btn_y = dock_y + 4U;
            uint32_t task_btn_h = dock_h - 8U;

            /* Squircle Icon Container */
            uint32_t task_bg = focused
                ? (g_light_theme ? UINT32_C(0x4038BDF8) : UINT32_C(0x3538BDF8))
                : (g_light_theme ? UINT32_C(0x15000000) : UINT32_C(0x18FFFFFF));
            uint32_t task_bdr = focused
                ? UINT32_C(0xFF60A5FA)
                : (g_light_theme ? UINT32_C(0x20000000) : UINT32_C(0x25FFFFFF));

            fill_rounded_rect(pixels, width, height, (int)task_x, (int)task_btn_y,
                              (int)TASK_WIDTH, (int)task_btn_h, 9,
                              task_bg, task_bdr);

            draw_app_icon(pixels, width, height, (int)task_x + 7, (int)task_btn_y + 3,
                          s->icon_name[0] ? s->icon_name :
                          (s->title[0] ? s->title : "Window"));

            /* Active / Focused Glowing Indicator Dot / Pill */
            if (focused) {
                fill_rounded_rect(pixels, width, height, (int)task_x + 13,
                                  (int)dock_y + (int)dock_h - 3, 16, 2, 1,
                                  UINT32_C(0xFF38BDF8), UINT32_C(0xFF38BDF8));
            } else {
                fill_rounded_rect(pixels, width, height, (int)task_x + 18,
                                  (int)dock_y + (int)dock_h - 3, 6, 2, 1,
                                  UINT32_C(0x8094A3B8), UINT32_C(0x8094A3B8));
            }

            task_x += TASK_WIDTH + TASK_GAP;
            ++drawn;
        }
    }

    /* 6. Floating Status Pill Tray (Right-Aligned) */
    if (width > tray_width + 24U) {
        uint32_t status_x = width - tray_width - 12U;
        uint32_t status_y = dock_y;
        uint32_t status_h = dock_h;

        /* Ambient Shadow */
        fill_rounded_rect(pixels, width, height, (int)status_x, (int)status_y + 2,
                          (int)tray_width, (int)status_h, 14,
                          UINT32_C(0x40000000), UINT32_C(0x20000000));

        /* Frosted Glass Tray Base */
        fill_rounded_rect(pixels, width, height, (int)status_x, (int)status_y,
                          (int)tray_width, (int)status_h, 14,
                          dock_fill, dock_border);

        uint32_t cursor_x = status_x + 14U;

        /* Network Indicator: Emerald dot for connected */
        if (caps && caps->net.available) {
            draw_wifi_icon(pixels, width, height, (int)cursor_x + 12, (int)status_y + (int)status_h / 2,
                           caps->net.connected);
            cursor_x += 32U;
        }

        /* Audio Indicator */
        if (caps && caps->audio) {
            draw_audio_icon(pixels, width, height, (int)cursor_x + 6, (int)status_y + 10);
            cursor_x += 32U;
        }

        /* Modern Digital Clock & Date */
        if (clock && clock->valid) {
            if (cursor_x > status_x + 16U) {
                fill_rect(pixels, width, height, (int)cursor_x, (int)status_y + 8, 1, status_h - 16U,
                          g_light_theme ? UINT32_C(0x30000000) : UINT32_C(0x30FFFFFF));
                cursor_x += 12U;
            }
            char time_text[8];
            char date_text[12];
            snprintf(time_text, sizeof(time_text), "%02d:%02d", clock->hour, clock->minute);
            snprintf(date_text, sizeof(date_text), "%02d/%02d/%02d",
                     clock->month, clock->day, clock->year % 100);

            draw_ui_text(pixels, width, height, (int)cursor_x, (int)status_y + 5, 1,
                         time_text,
                         g_light_theme ? UINT32_C(0xFF0F172A) : UINT32_C(0xFFF8FAFC), 58);
            draw_ui_text(pixels, width, height, (int)cursor_x, (int)status_y + 20, 1,
                         date_text,
                         g_light_theme ? UINT32_C(0xFF475569) : UINT32_C(0xFF94A3B8), 104);
        }
    }
}

#endif /* AURORA_PANEL_H */
