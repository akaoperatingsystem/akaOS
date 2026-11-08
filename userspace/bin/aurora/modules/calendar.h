#ifndef AURORA_CALENDAR_H
#define AURORA_CALENDAR_H

#include "aurora_types.h"
#include "draw.h"

static inline int leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static inline int clock_refresh(struct aurora_clock *clock) {
    char text[32];
    if (!read_text_file("/proc/realtime", text, sizeof(text))) {
        clock->valid = 0;
        return 0;
    }
    uint32_t epoch = 0;
    size_t index = 0;
    while (text[index] >= '0' && text[index] <= '9') {
        uint32_t digit = (uint32_t)(text[index++] - '0');
        if (epoch > (UINT32_MAX - digit) / 10U) {
            clock->valid = 0;
            return 0;
        }
        epoch = epoch * 10U + digit;
    }
    if (index == 0 || epoch == 0) {
        clock->valid = 0;
        return 0;
    }

    /* Optional local time offset if configured in /etc/akaos.conf or default UTC */
    uint32_t days = epoch / 86400U;
    uint32_t remaining = epoch % 86400U;
    int year = 1970;
    for (;;) {
        uint32_t year_days = leap_year(year) ? 366U : 365U;
        if (days < year_days) break;
        days -= year_days;
        ++year;
    }
    static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};
    int month = 1;
    while (month <= 12) {
        uint32_t count = month == 2 && leap_year(year) ? 29U : month_days[month - 1];
        if (days < count) break;
        days -= count;
        ++month;
    }
    clock->year = year;
    clock->month = month;
    clock->day = (int)days + 1;
    clock->hour = (int)(remaining / 3600U);
    remaining %= 3600U;
    clock->minute = (int)(remaining / 60U);
    clock->second = (int)(remaining % 60U);
    clock->valid = 1;
    return 1;
}

static inline const char *month_name(int m) {
    static const char *names[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    if (m >= 1 && m <= 12) return names[m - 1];
    return "Month";
}

/* Calculate day of week (0=Sunday, 1=Monday, ..., 6=Saturday) for year/month/day */
static inline int day_of_week(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

static inline void render_calendar_popup(uint32_t *pixels, uint32_t width, uint32_t height,
                                         const struct aurora_clock *clock) {
    /* Pop-up calendar window background */
    fill_rounded_rect(pixels, width, height, 0, 0, (int)width, (int)height, 12,
                      g_light_theme ? UINT32_C(0xf6f8fb) : UINT32_C(0x151e2b),
                      g_light_theme ? UINT32_C(0xbcc8d6) : UINT32_C(0x4c5e77));

    if (!clock || !clock->valid) {
        draw_ui_text(pixels, width, height, 20, 20, 1, "Clock unavailable", UINT32_C(0x8898aa), 150);
        return;
    }

    /* Header: Month Year + Digital Clock */
    char header_buf[32];
    snprintf(header_buf, sizeof(header_buf), "%s %d", month_name(clock->month), clock->year);
    draw_ui_text(pixels, width, height, 16, 12, 1, header_buf,
                 g_light_theme ? UINT32_C(0x19273a) : UINT32_C(0xf2f6fb), (int)width - 80);

    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", clock->hour, clock->minute, clock->second);
    draw_ui_text(pixels, width, height, (int)width - 70, 12, 1, time_buf,
                 UINT32_C(0x60a5fa), 65);

    /* Mode button: UTC */
    fill_rounded_rect(pixels, width, height, 14, 34, 40, 18, 4,
                      UINT32_C(0x233144), UINT32_C(0x3d506a));
    draw_ui_text(pixels, width, height, 22, 36, 1, "UTC", UINT32_C(0x86efac), 30);

    /* Day of week headers: Su Mo Tu We Th Fr Sa */
    static const char *day_headers[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    int grid_start_x = 14;
    int grid_start_y = 60;
    int cell_w = ((int)width - 28) / 7;
    int cell_h = 22;

    for (int d = 0; d < 7; ++d) {
        draw_ui_text(pixels, width, height, grid_start_x + d * cell_w + 4, grid_start_y, 1,
                     day_headers[d], UINT32_C(0x76879d), cell_w - 4);
    }

    /* Days of current month */
    static const uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int total_days = (clock->month == 2 && leap_year(clock->year)) ? 29 : month_days[clock->month - 1];
    int first_dow = day_of_week(clock->year, clock->month, 1);

    int cur_dow = first_dow;
    int row_y = grid_start_y + 18;

    for (int day = 1; day <= total_days; ++day) {
        int cell_x = grid_start_x + cur_dow * cell_w;
        char num_buf[4];
        snprintf(num_buf, sizeof(num_buf), "%d", day);

        if (day == clock->day) {
            /* Highlight today's date with accent badge */
            fill_rounded_rect(pixels, width, height, cell_x + 1, row_y - 2, cell_w - 2, cell_h - 2, 5,
                              UINT32_C(0x2563eb), UINT32_C(0x60a5fa));
            draw_ui_text(pixels, width, height, cell_x + (day < 10 ? 8 : 4), row_y + 1, 1,
                         num_buf, UINT32_C(0xffffff), cell_w);
        } else {
            draw_ui_text(pixels, width, height, cell_x + (day < 10 ? 8 : 4), row_y + 1, 1,
                         num_buf,
                         g_light_theme ? UINT32_C(0x223247) : UINT32_C(0xdde5f0),
                         cell_w);
        }

        cur_dow++;
        if (cur_dow == 7) {
            cur_dow = 0;
            row_y += cell_h;
        }
    }

    /* Footer status: No upcoming schedule conflicts */
    int foot_y = (int)height - 22;
    fill_rect(pixels, width, height, 14, foot_y - 4, (int)width - 28, 1,
              g_light_theme ? UINT32_C(0xd0d8e2) : UINT32_C(0x2d3d52));
    draw_ui_text(pixels, width, height, 16, foot_y, 1, "Schedule: System synchronized (RTC)",
                 UINT32_C(0x76879d), (int)width - 32);
}

#endif /* AURORA_CALENDAR_H */
