#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

static void print_usage(void) {
    printf("Usage: sleep NUMBER[SUFFIX]...\n");
    printf("Pause for NUMBER seconds. SUFFIX may be 's' for seconds (the default),\n");
    printf("'m' for minutes, 'h' for hours, 'd' for days, or 'ms' for milliseconds.\n\n");
    printf("Options:\n");
    printf("  -h, --help     display this help and exit\n");
}

static int parse_duration_ms(const char *str, int *out_ms) {
    if (!str || !*str) return -1;
    const char *p = str;
    int whole = 0;
    int frac_ms = 0;
    int seen_digits = 0;

    while (*p >= '0' && *p <= '9') {
        whole = whole * 10 + (*p - '0');
        seen_digits = 1;
        p++;
    }

    if (*p == '.') {
        p++;
        int mult = 100;
        while (*p >= '0' && *p <= '9') {
            seen_digits = 1;
            if (mult > 0) {
                frac_ms += (*p - '0') * mult;
                mult /= 10;
            }
            p++;
        }
    }

    if (!seen_digits) return -1;

    int multiplier_ms = 1000;
    if (*p == '\0' || strcmp(p, "s") == 0) {
        multiplier_ms = 1000;
    } else if (strcmp(p, "ms") == 0) {
        multiplier_ms = 1;
        frac_ms = 0;
    } else if (strcmp(p, "m") == 0) {
        multiplier_ms = 60 * 1000;
        frac_ms *= 60;
    } else if (strcmp(p, "h") == 0) {
        multiplier_ms = 3600 * 1000;
        frac_ms *= 3600;
    } else if (strcmp(p, "d") == 0) {
        multiplier_ms = 86400 * 1000;
        frac_ms *= 86400;
    } else {
        return -1;
    }

    *out_ms = whole * multiplier_ms + frac_ms;
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "sleep: missing operand\n");
        fprintf(stderr, "Try 'sleep --help' for more information.\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        }
    }

    int total_ms = 0;
    for (int i = 1; i < argc; i++) {
        int ms = 0;
        if (parse_duration_ms(argv[i], &ms) != 0) {
            fprintf(stderr, "sleep: invalid time interval '%s'\n", argv[i]);
            return 1;
        }
        total_ms += ms;
    }

    if (total_ms > 0) {
        poll(NULL, 0, total_ms);
    }
    return 0;
}
