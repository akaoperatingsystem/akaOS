#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char *const month_names[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const int days_in_months[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int get_days_in_month(int year, int month) {
    if (month == 2 && is_leap_year(year)) return 29;
    if (month >= 1 && month <= 12) return days_in_months[month - 1];
    return 30;
}

static int day_of_year_start(int year, int month) {
    int d = 0;
    for (int m = 1; m < month; ++m) {
        d += get_days_in_month(year, m);
    }
    return d + 1;
}

/* Day of week for 1st of month: 0 = Sun, 1 = Mon, ..., 6 = Sat */
static int day_of_week(int year, int month, int day) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    mktime(&t);
    return t.tm_wday;
}

struct month_grid {
    char header[32];
    char lines[7][32]; /* week headers + up to 6 week rows */
};

/* Format a month grid */
static void format_month(int year, int month, int monday_first, int julian, struct month_grid *g) {
    memset(g, 0, sizeof(*g));

    char title[32];
    snprintf(title, sizeof(title), "%s %d", month_names[month - 1], year);
    int title_len = (int)strlen(title);
    int width = julian ? 28 : 20;
    int pad = (width - title_len) / 2;
    if (pad < 0) pad = 0;
    snprintf(g->header, sizeof(g->header), "%*s%s", pad, "", title);

    if (julian) {
        if (monday_first) {
            strncpy(g->lines[0], " Mo  Tu  We  Th  Fr  Sa  Su", sizeof(g->lines[0]) - 1);
        } else {
            strncpy(g->lines[0], " Su  Mo  Tu  We  Th  Fr  Sa", sizeof(g->lines[0]) - 1);
        }
    } else {
        if (monday_first) {
            strncpy(g->lines[0], "Mo Tu We Th Fr Sa Su", sizeof(g->lines[0]) - 1);
        } else {
            strncpy(g->lines[0], "Su Mo Tu We Th Fr Sa", sizeof(g->lines[0]) - 1);
        }
    }

    int start_wday = day_of_week(year, month, 1);
    if (monday_first) {
        start_wday = (start_wday + 6) % 7;
    }

    int total_days = get_days_in_month(year, month);
    int jday = day_of_year_start(year, month);

    int row = 1;
    int col = 0;
    char cur_line[64] = "";

    for (int i = 0; i < start_wday; ++i) {
        if (julian) {
            strcat(cur_line, "    ");
        } else {
            if (i > 0) strcat(cur_line, " ");
            strcat(cur_line, "  ");
        }
        col++;
    }

    for (int day = 1; day <= total_days; ++day) {
        char item[16];
        if (julian) {
            snprintf(item, sizeof(item), "%3d ", jday++);
        } else {
            if (col == 0) snprintf(item, sizeof(item), "%2d", day);
            else snprintf(item, sizeof(item), " %2d", day);
        }
        strcat(cur_line, item);
        col++;
        if (col == 7) {
            strncpy(g->lines[row++], cur_line, sizeof(g->lines[row]) - 1);
            cur_line[0] = '\0';
            col = 0;
        }
    }
    if (col > 0) {
        while (col < 7) {
            if (julian) strcat(cur_line, "    ");
            else strcat(cur_line, "   ");
            col++;
        }
        strncpy(g->lines[row++], cur_line, sizeof(g->lines[row]) - 1);
    }
}

static void print_single_month(int year, int month, int monday_first, int julian) {
    struct month_grid g;
    format_month(year, month, monday_first, julian, &g);

    printf("%s\n", g.header);
    for (int i = 0; i < 7; ++i) {
        if (g.lines[i][0]) {
            printf("%s\n", g.lines[i]);
        }
    }
}

static void print_three_months(int year, int month, int monday_first, int julian) {
    int prev_year = year, prev_month = month - 1;
    if (prev_month < 1) { prev_month = 12; prev_year--; }

    int next_year = year, next_month = month + 1;
    if (next_month > 12) { next_month = 1; next_year++; }

    struct month_grid g[3];
    format_month(prev_year, prev_month, monday_first, julian, &g[0]);
    format_month(year, month, monday_first, julian, &g[1]);
    format_month(next_year, next_month, monday_first, julian, &g[2]);

    int w = julian ? 28 : 20;
    printf("%-*s  %-*s  %-*s\n", w, g[0].header, w, g[1].header, w, g[2].header);
    for (int line = 0; line < 7; ++line) {
        char l0[32], l1[32], l2[32];
        snprintf(l0, sizeof(l0), "%-*s", w, g[0].lines[line]);
        snprintf(l1, sizeof(l1), "%-*s", w, g[1].lines[line]);
        snprintf(l2, sizeof(l2), "%-*s", w, g[2].lines[line]);
        if (g[0].lines[line][0] || g[1].lines[line][0] || g[2].lines[line][0]) {
            printf("%s  %s  %s\n", l0, l1, l2);
        }
    }
}

static void print_year(int year, int monday_first, int julian) {
    int w = julian ? 28 : 20;
    int banner_pad = (w * 3 + 4 - 4) / 2;
    if (banner_pad < 0) banner_pad = 0;
    printf("%*s%d\n\n", banner_pad, "", year);

    for (int q = 0; q < 4; ++q) {
        struct month_grid g[3];
        for (int m = 0; m < 3; ++m) {
            int month_idx = q * 3 + m + 1;
            format_month(year, month_idx, monday_first, julian, &g[m]);
        }

        printf("%-*s  %-*s  %-*s\n", w, g[0].header, w, g[1].header, w, g[2].header);
        for (int line = 0; line < 7; ++line) {
            char l0[32], l1[32], l2[32];
            snprintf(l0, sizeof(l0), "%-*s", w, g[0].lines[line]);
            snprintf(l1, sizeof(l1), "%-*s", w, g[1].lines[line]);
            snprintf(l2, sizeof(l2), "%-*s", w, g[2].lines[line]);
            if (g[0].lines[line][0] || g[1].lines[line][0] || g[2].lines[line][0]) {
                printf("%s  %s  %s\n", l0, l1, l2);
            }
        }
        printf("\n");
    }
}

/* ncal layout: vertical days with horizontal weeks */
static void print_ncal_month(int year, int month, int monday_first) {
    char title[32];
    snprintf(title, sizeof(title), "    %s %d", month_names[month - 1], year);
    printf("%s\n", title);

    static const char *const day_labels_sun[] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
    static const char *const day_labels_mon[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
    const char *const *labels = monday_first ? day_labels_mon : day_labels_sun;

    int total_days = get_days_in_month(year, month);
    for (int d = 0; d < 7; ++d) {
        printf("%s", labels[d]);
        for (int day = 1; day <= total_days; ++day) {
            int w = day_of_week(year, month, day);
            if (monday_first) w = (w + 6) % 7;
            if (w == d) {
                /* Calculate week column */
                int start_w = day_of_week(year, month, 1);
                if (monday_first) start_w = (start_w + 6) % 7;
                int col = (day + start_w - 1) / 7;
                int spaces = (col == 0) ? 1 : 1;
                (void)spaces;
                printf(" %2d", day);
            } else if (day == 1) {
                int start_w = day_of_week(year, month, 1);
                if (monday_first) start_w = (start_w + 6) % 7;
                if (d < start_w) {
                    printf("   ");
                }
            }
        }
        printf("\n");
    }
}

static void print_version(void) {
    printf("cal (akaOS util-linux) 0.2.66\n");
}

int main(int argc, char **argv) {
    const char *progname = (argc > 0) ? argv[0] : "cal";
    const char *slash = strrchr(progname, '/');
    if (slash) progname = slash + 1;

    int is_ncal = (strstr(progname, "ncal") != NULL);
    int opt_1 = 0;
    (void)opt_1;
    int opt_3 = 0;
    int opt_sunday = 0;
    int opt_monday = is_ncal ? 1 : 0;
    int opt_julian = 0;
    int opt_year = 0;

    int operands[3];
    int operand_count = 0;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            printf("Usage: cal [-smjy13] [[[day] month] year]\n");
            return 0;
        }
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-V") == 0) {
            print_version();
            return 0;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            for (size_t c = 1; arg[c]; ++c) {
                if (arg[c] == '1') { opt_1 = 1; opt_3 = 0; opt_year = 0; }
                else if (arg[c] == '3') { opt_3 = 1; opt_1 = 0; opt_year = 0; }
                else if (arg[c] == 's') { opt_sunday = 1; opt_monday = 0; }
                else if (arg[c] == 'm') { opt_monday = 1; opt_sunday = 0; }
                else if (arg[c] == 'j') { opt_julian = 1; }
                else if (arg[c] == 'y') { opt_year = 1; opt_1 = 0; opt_3 = 0; }
            }
            continue;
        }
        if (operand_count < 3 && isdigit((unsigned char)arg[0])) {
            operands[operand_count++] = atoi(arg);
        }
    }

    time_t now = time(NULL);
    struct tm cur_tm;
    localtime_r(&now, &cur_tm);
    int cur_year = cur_tm.tm_year + 1900;
    int cur_month = cur_tm.tm_mon + 1;

    int target_year = cur_year;
    int target_month = cur_month;

    if (operand_count == 1) {
        target_year = operands[0];
        opt_year = 1;
    } else if (operand_count == 2) {
        target_month = operands[0];
        target_year = operands[1];
    } else if (operand_count == 3) {
        /* day month year */
        target_month = operands[1];
        target_year = operands[2];
    }

    if (target_month < 1 || target_month > 12) {
        fprintf(stderr, "cal: illegal month value: use 1-12\n");
        return 1;
    }
    if (target_year < 1 || target_year > 9999) {
        fprintf(stderr, "cal: illegal year value: use 1-9999\n");
        return 1;
    }

    int monday_first = opt_monday;
    if (opt_sunday) monday_first = 0;

    if (is_ncal && !opt_year && !opt_3) {
        print_ncal_month(target_year, target_month, monday_first);
        return 0;
    }

    if (opt_year) {
        print_year(target_year, monday_first, opt_julian);
    } else if (opt_3) {
        print_three_months(target_year, target_month, monday_first, opt_julian);
    } else {
        print_single_month(target_year, target_month, monday_first, opt_julian);
    }

    return 0;
}
