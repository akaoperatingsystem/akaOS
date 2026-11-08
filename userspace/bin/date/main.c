#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DATE_VERSION "8.32 (akaOS coreutils)"

static const char *const weekdays_short[] =
    {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *const weekdays_long[] =
    {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char *const months_short[] =
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
static const char *const months_long[] =
    {"January", "February", "March", "April", "May", "June",
     "July", "August", "September", "October", "November", "December"};

static int opt_utc = 0;
static const char *opt_date_str = NULL;
static const char *opt_file_str = NULL;
static const char *opt_reference = NULL;
static const char *opt_iso_8601 = NULL;
static const char *opt_rfc_3339 = NULL;
static int opt_rfc_822 = 0;


static void print_version(void) {
    printf("date %s\n", DATE_VERSION);
}

static void print_usage(void) {
    printf("Usage: date [OPTION]... [+FORMAT]\n"
           "  or:  date [-u|--utc|--universal] [MMDDhhmm[[CC]YY][.ss]]\n"
           "Display the current time in the given FORMAT, or set the system date.\n\n"
           "  -d, --date=STRING         display time described by STRING, not 'now'\n"
           "  -f, --file=DATEFILE       like --date; once for each line of DATEFILE\n"
           "  -I[FMT], --iso-8601[=FMT] output date/time in ISO 8601 format.\n"
           "                              FMT='date' (default), 'hours', 'minutes',\n"
           "                              or 'seconds'\n"
           "  -r, --reference=FILE      display the last modification time of FILE\n"
           "  -R, --rfc-email           output date and time in RFC 5322 format\n"
           "      --rfc-3339=FMT        output date/time in RFC 3339 format:\n"
           "                              FMT='date', 'seconds', or 'ns'\n"
           "  -s, --set=STRING          set time described by STRING\n"
           "  -u, --utc, --universal    print or set Coordinated Universal Time (UTC)\n"
           "      --help                display this help and exit\n"
           "      --version             output version information and exit\n");
}

static int is_leap(int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static int iso_week_number(const struct tm *tm) {
    /* Day of year (0-based) */
    int yday = tm->tm_yday;
    /* Day of week with Monday = 0..Sunday = 6 */
    int wday = (tm->tm_wday + 6) % 7;
    int d = yday - wday + 3;
    if (d < 0) {
        int prev_year = tm->tm_year + 1900 - 1;
        return is_leap(prev_year) ? 53 : 52;
    }
    int week = d / 7 + 1;
    if (week == 53) {
        int year = tm->tm_year + 1900;
        int leap = is_leap(year);
        int jan1_wday = (wday - (yday % 7) + 7) % 7;
        if (jan1_wday != 3 && (jan1_wday != 2 || !leap)) {
            week = 1;
        }
    }
    return week;
}

static int iso_week_year(const struct tm *tm) {
    int year = tm->tm_year + 1900;
    int week = iso_week_number(tm);
    if (tm->tm_mon == 0 && week >= 52) return year - 1;
    if (tm->tm_mon == 11 && week == 1) return year + 1;
    return year;
}

static time_t parse_date_string(const char *str, time_t now) {
    if (!str || !*str) return now;

    /* Check for epoch @seconds */
    if (str[0] == '@') {
        return (time_t)atoll(str + 1);
    }

    /* Check for timezone prefix e.g. TZ="America/Los_Angeles" ... */
    if (strncmp(str, "TZ=", 3) == 0) {
        const char *p = str + 3;
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            const char *endq = strchr(p, quote);
            if (endq) {
                char tzbuf[64];
                size_t len = (size_t)(endq - p);
                if (len < sizeof(tzbuf)) {
                    strncpy(tzbuf, p, len);
                    tzbuf[len] = '\0';
                    setenv("TZ", tzbuf, 1);
                    tzset();
                }
                str = endq + 1;
                while (*str == ' ' || *str == '\t') str++;
            }
        }
    }

    struct tm tm;
    if (opt_utc) {
        gmtime_r(&now, &tm);
    } else {
        localtime_r(&now, &tm);
    }

    /* Check special keywords */
    if (strcasecmp(str, "now") == 0) return now;
    if (strcasecmp(str, "yesterday") == 0) return now - 86400;
    if (strcasecmp(str, "tomorrow") == 0) return now + 86400;

    /* Relative offsets: e.g. "2 days ago", "3 months 1 day", "+2 minutes", "-1 hour" */
    char buf[256];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int sign = 1;
    char *p = buf;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '+') { sign = 1; p++; }
    else if (*p == '-') { sign = -1; p++; }

    int year = 0, mon = 0, day = 0, hour = 0, min = 0, sec = 0;

    /* Check YYYY-MM-DD HH:MM:SS or YYYY-MM-DD */
    if (sscanf(p, "%d-%d-%d %d:%d:%d", &year, &mon, &day, &hour, &min, &sec) == 6) {
        tm.tm_year = year - 1900;
        tm.tm_mon = mon - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = min;
        tm.tm_sec = sec;
        tm.tm_isdst = -1;
        return mktime(&tm);
    }
    if (sscanf(p, "%d-%d-%d", &year, &mon, &day) == 3) {
        tm.tm_year = year - 1900;
        tm.tm_mon = mon - 1;
        tm.tm_mday = day;
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;
        return mktime(&tm);
    }

    /* Check month name & day e.g. "Feb 1", "25 Dec", "1may" */
    char mname[32];
    if (sscanf(p, "%31[A-Za-z] %d", mname, &day) == 2 ||
        sscanf(p, "%d %31[A-Za-z]", &day, mname) == 2 ||
        sscanf(p, "%d%31[A-Za-z]", &day, mname) == 2) {
        for (int m = 0; m < 12; ++m) {
            if (strncasecmp(mname, months_short[m], 3) == 0 ||
                strcasecmp(mname, months_long[m]) == 0) {
                tm.tm_mon = m;
                tm.tm_mday = day;
                tm.tm_hour = 0;
                tm.tm_min = 0;
                tm.tm_sec = 0;
                tm.tm_isdst = -1;
                return mktime(&tm);
            }
        }
    }

    /* Check number followed by unit (day, hour, min, sec, month, year) */
    int val = 0;
    char unit[64];
    if (sscanf(p, "%d %63s", &val, unit) == 2 || sscanf(p, "%d%63s", &val, unit) == 2) {
        val *= sign;
        if (strstr(p, "ago")) val = -abs(val);
        if (strncasecmp(unit, "day", 3) == 0) return now + (time_t)val * 86400;
        if (strncasecmp(unit, "hour", 4) == 0) return now + (time_t)val * 3600;
        if (strncasecmp(unit, "min", 3) == 0) return now + (time_t)val * 60;
        if (strncasecmp(unit, "sec", 3) == 0) return now + (time_t)val;
        if (strncasecmp(unit, "month", 5) == 0) {
            tm.tm_mon += val;
            while (tm.tm_mon < 0) { tm.tm_mon += 12; tm.tm_year--; }
            while (tm.tm_mon > 11) { tm.tm_mon -= 12; tm.tm_year++; }
            tm.tm_isdst = -1;
            return mktime(&tm);
        }
        if (strncasecmp(unit, "year", 4) == 0) {
            tm.tm_year += val;
            tm.tm_isdst = -1;
            return mktime(&tm);
        }
    }

    return now;
}

/* Parse touch-style MMDDhhmm[[CC]YY][.ss] */
static time_t parse_set_format(const char *arg, time_t now) {
    struct tm tm;
    if (opt_utc) gmtime_r(&now, &tm);
    else localtime_r(&now, &tm);

    size_t len = strlen(arg);
    const char *dot = strchr(arg, '.');
    int ss = 0;
    if (dot) {
        ss = atoi(dot + 1);
        len = (size_t)(dot - arg);
    }

    char buf[16];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    strncpy(buf, arg, len);
    buf[len] = '\0';

    int MM = 0, DD = 0, hh = 0, mm = 0, year = tm.tm_year + 1900;
    if (len == 8) {
        /* MMDDhhmm */
        sscanf(buf, "%2d%2d%2d%2d", &MM, &DD, &hh, &mm);
    } else if (len == 10) {
        /* MMDDhhmmYY */
        int YY = 0;
        sscanf(buf, "%2d%2d%2d%2d%2d", &MM, &DD, &hh, &mm, &YY);
        year = (YY >= 69) ? (1900 + YY) : (2000 + YY);
    } else if (len == 12) {
        /* MMDDhhmmCCYY */
        sscanf(buf, "%2d%2d%2d%2d%4d", &MM, &DD, &hh, &mm, &year);
    } else {
        return (time_t)-1;
    }

    tm.tm_mon = MM - 1;
    tm.tm_mday = DD;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    tm.tm_sec = ss;
    tm.tm_year = year - 1900;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

static void format_and_print_time(time_t t, const char *fmt) {
    struct tm tm;
    if (opt_utc) {
        gmtime_r(&t, &tm);
    } else {
        localtime_r(&t, &tm);
    }

    if (!fmt) {
        /* Default format: e.g. "Sun Nov  8 21:05:25 UTC 2026" or locale %c */
        char def[128];
        snprintf(def, sizeof(def), "%s %s %2d %02d:%02d:%02d %s %04d",
                 weekdays_short[tm.tm_wday], months_short[tm.tm_mon],
                 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                 opt_utc ? "UTC" : (tm.tm_zone ? tm.tm_zone : "UTC"),
                 tm.tm_year + 1900);
        puts(def);
        return;
    }

    char out[1024];
    size_t opos = 0;

    for (size_t i = 0; fmt[i] && opos + 64 < sizeof(out); ++i) {
        if (fmt[i] != '%') {
            out[opos++] = fmt[i];
            continue;
        }

        i++;
        char pad = '0';
        int pad_explicit = 0;
        int upper = 0;

        /* Check GNU flags: -, _, 0, ^, # */
        while (fmt[i] == '-' || fmt[i] == '_' || fmt[i] == '0' || fmt[i] == '^' || fmt[i] == '#') {
            if (fmt[i] == '-') { pad = '\0'; pad_explicit = 1; }
            else if (fmt[i] == '_') { pad = ' '; pad_explicit = 1; }
            else if (fmt[i] == '0') { pad = '0'; pad_explicit = 1; }
            else if (fmt[i] == '^') { upper = 1; }
            i++;
        }

        /* Check width */
        int width = 0;
        while (isdigit(fmt[i])) {
            width = width * 10 + (fmt[i] - '0');
            i++;
        }

        char spec = fmt[i];
        if (!spec) break;

        char piece[128] = "";

        switch (spec) {
            case '%': strcpy(piece, "%"); break;
            case 'a': strncpy(piece, weekdays_short[tm.tm_wday], sizeof(piece)-1); break;
            case 'A': strncpy(piece, weekdays_long[tm.tm_wday], sizeof(piece)-1); break;
            case 'b': case 'h': strncpy(piece, months_short[tm.tm_mon], sizeof(piece)-1); break;
            case 'B': strncpy(piece, months_long[tm.tm_mon], sizeof(piece)-1); break;
            case 'c':
                snprintf(piece, sizeof(piece), "%s %s %2d %02d:%02d:%02d %04d",
                         weekdays_short[tm.tm_wday], months_short[tm.tm_mon],
                         tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_year + 1900);
                break;
            case 'C': snprintf(piece, sizeof(piece), "%02d", (tm.tm_year + 1900) / 100); break;
            case 'd':
                if (pad_explicit && pad == '\0') snprintf(piece, sizeof(piece), "%d", tm.tm_mday);
                else if (pad_explicit && pad == ' ') snprintf(piece, sizeof(piece), "%2d", tm.tm_mday);
                else snprintf(piece, sizeof(piece), "%02d", tm.tm_mday);
                break;
            case 'D':
                snprintf(piece, sizeof(piece), "%02d/%02d/%02d",
                         tm.tm_mon + 1, tm.tm_mday, (tm.tm_year + 1900) % 100);
                break;
            case 'e': snprintf(piece, sizeof(piece), "%2d", tm.tm_mday); break;
            case 'F':
                snprintf(piece, sizeof(piece), "%04d-%02d-%02d",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
                break;
            case 'g': snprintf(piece, sizeof(piece), "%02d", iso_week_year(&tm) % 100); break;
            case 'G': snprintf(piece, sizeof(piece), "%04d", iso_week_year(&tm)); break;
            case 'H':
                if (pad_explicit && pad == '\0') snprintf(piece, sizeof(piece), "%d", tm.tm_hour);
                else if (pad_explicit && pad == ' ') snprintf(piece, sizeof(piece), "%2d", tm.tm_hour);
                else snprintf(piece, sizeof(piece), "%02d", tm.tm_hour);
                break;
            case 'I': {
                int h = tm.tm_hour % 12;
                if (h == 0) h = 12;
                if (pad_explicit && pad == '\0') snprintf(piece, sizeof(piece), "%d", h);
                else if (pad_explicit && pad == ' ') snprintf(piece, sizeof(piece), "%2d", h);
                else snprintf(piece, sizeof(piece), "%02d", h);
                break;
            }
            case 'j': snprintf(piece, sizeof(piece), "%03d", tm.tm_yday + 1); break;
            case 'k': snprintf(piece, sizeof(piece), "%2d", tm.tm_hour); break;
            case 'l': {
                int h = tm.tm_hour % 12;
                if (h == 0) h = 12;
                snprintf(piece, sizeof(piece), "%2d", h);
                break;
            }
            case 'm':
                if (pad_explicit && pad == '\0') snprintf(piece, sizeof(piece), "%d", tm.tm_mon + 1);
                else if (pad_explicit && pad == ' ') snprintf(piece, sizeof(piece), "%2d", tm.tm_mon + 1);
                else snprintf(piece, sizeof(piece), "%02d", tm.tm_mon + 1);
                break;
            case 'M':
                if (pad_explicit && pad == '\0') snprintf(piece, sizeof(piece), "%d", tm.tm_min);
                else snprintf(piece, sizeof(piece), "%02d", tm.tm_min);
                break;
            case 'n': strcpy(piece, "\n"); break;
            case 'N': strcpy(piece, "000000000"); break;
            case 'p': strcpy(piece, tm.tm_hour < 12 ? "AM" : "PM"); break;
            case 'P': strcpy(piece, tm.tm_hour < 12 ? "am" : "pm"); break;
            case 'q': snprintf(piece, sizeof(piece), "%d", (tm.tm_mon / 3) + 1); break;
            case 'r': {
                int h = tm.tm_hour % 12;
                if (h == 0) h = 12;
                snprintf(piece, sizeof(piece), "%02d:%02d:%02d %s", h, tm.tm_min, tm.tm_sec, tm.tm_hour < 12 ? "AM" : "PM");
                break;
            }
            case 'R': snprintf(piece, sizeof(piece), "%02d:%02d", tm.tm_hour, tm.tm_min); break;
            case 's': snprintf(piece, sizeof(piece), "%lld", (long long)t); break;
            case 'S':
                if (pad_explicit && pad == '\0') snprintf(piece, sizeof(piece), "%d", tm.tm_sec);
                else snprintf(piece, sizeof(piece), "%02d", tm.tm_sec);
                break;
            case 't': strcpy(piece, "\t"); break;
            case 'T': snprintf(piece, sizeof(piece), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec); break;
            case 'u': snprintf(piece, sizeof(piece), "%d", tm.tm_wday == 0 ? 7 : tm.tm_wday); break;
            case 'U': snprintf(piece, sizeof(piece), "%02d", (tm.tm_yday - tm.tm_wday + 7) / 7); break;
            case 'V': snprintf(piece, sizeof(piece), "%02d", iso_week_number(&tm)); break;
            case 'w': snprintf(piece, sizeof(piece), "%d", tm.tm_wday); break;
            case 'W': {
                int wday_mon = (tm.tm_wday + 6) % 7;
                snprintf(piece, sizeof(piece), "%02d", (tm.tm_yday - wday_mon + 7) / 7);
                break;
            }
            case 'x': snprintf(piece, sizeof(piece), "%02d/%02d/%02d", tm.tm_mon + 1, tm.tm_mday, (tm.tm_year + 1900) % 100); break;
            case 'X': snprintf(piece, sizeof(piece), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec); break;
            case 'y': snprintf(piece, sizeof(piece), "%02d", (tm.tm_year + 1900) % 100); break;
            case 'Y': snprintf(piece, sizeof(piece), "%04d", tm.tm_year + 1900); break;
            case 'z': {
                if (fmt[i + 1] == ':') {
                    i++;
                    if (fmt[i + 1] == ':') {
                        i++;
                        strcpy(piece, "+00:00:00");
                    } else {
                        strcpy(piece, "+00:00");
                    }
                } else {
                    strcpy(piece, "+0000");
                }
                break;
            }
            case 'Z': strcpy(piece, opt_utc ? "UTC" : (tm.tm_zone ? tm.tm_zone : "UTC")); break;
            default:
                piece[0] = '%';
                piece[1] = spec;
                piece[2] = '\0';
                break;
        }

        if (upper) {
            for (char *c = piece; *c; ++c) *c = (char)toupper((unsigned char)*c);
        }

        for (size_t pi = 0; piece[pi] && opos < sizeof(out) - 1; ++pi) {
            out[opos++] = piece[pi];
        }
    }

    out[opos] = '\0';
    puts(out);
}

int main(int argc, char **argv) {
    const char *format_arg = NULL;
    const char *set_arg = NULL;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-u") == 0 || strcmp(arg, "--utc") == 0 || strcmp(arg, "--universal") == 0) {
            opt_utc = 1;
            continue;
        }
        if (strcmp(arg, "-R") == 0 || strcmp(arg, "--rfc-822") == 0 || strcmp(arg, "--rfc-email") == 0) {
            opt_rfc_822 = 1;
            continue;
        }
        if (strncmp(arg, "--rfc-3339=", 11) == 0) {
            opt_rfc_3339 = arg + 11;
            continue;
        }
        if (strcmp(arg, "-I") == 0 || strcmp(arg, "--iso-8601") == 0) {
            opt_iso_8601 = "date";
            continue;
        }
        if (strncmp(arg, "-I", 2) == 0) {
            opt_iso_8601 = arg + 2;
            continue;
        }
        if (strncmp(arg, "--iso-8601=", 11) == 0) {
            opt_iso_8601 = arg + 11;
            continue;
        }
        if ((strcmp(arg, "-d") == 0 || strcmp(arg, "--date") == 0) && i + 1 < argc) {
            opt_date_str = argv[++i];
            continue;
        }
        if (strncmp(arg, "-d", 2) == 0 && arg[2] != '\0') {
            opt_date_str = arg + 2;
            continue;
        }
        if (strncmp(arg, "--date=", 7) == 0) {
            opt_date_str = arg + 7;
            continue;
        }
        if ((strcmp(arg, "-f") == 0 || strcmp(arg, "--file") == 0) && i + 1 < argc) {
            opt_file_str = argv[++i];
            continue;
        }
        if (strncmp(arg, "--file=", 7) == 0) {
            opt_file_str = arg + 7;
            continue;
        }
        if ((strcmp(arg, "-r") == 0 || strcmp(arg, "--reference") == 0) && i + 1 < argc) {
            opt_reference = argv[++i];
            continue;
        }
        if (strncmp(arg, "--reference=", 12) == 0) {
            opt_reference = arg + 12;
            continue;
        }
        if ((strcmp(arg, "-s") == 0 || strcmp(arg, "--set") == 0) && i + 1 < argc) {
            set_arg = argv[++i];
            continue;
        }
        if (strncmp(arg, "--set=", 6) == 0) {
            set_arg = arg + 6;
            continue;
        }
        if (arg[0] == '+') {
            format_arg = arg + 1;
            continue;
        }

        /* Standalone MMDDhhmm[[CC]YY][.ss] */
        if (isdigit(arg[0])) {
            set_arg = arg;
            continue;
        }
    }

    time_t target_time = time(NULL);

    if (opt_reference) {
        struct stat st;
        if (stat(opt_reference, &st) != 0) {
            fprintf(stderr, "date: %s: %s\n", opt_reference, strerror(errno));
            return 1;
        }
        target_time = st.st_mtime;
    }

    if (set_arg) {
        time_t new_time = parse_set_format(set_arg, target_time);
        if (new_time == (time_t)-1) {
            new_time = parse_date_string(set_arg, target_time);
        }
        /* Setting the date */
        target_time = new_time;
    }

    if (opt_date_str) {
        target_time = parse_date_string(opt_date_str, target_time);
    }

    if (opt_file_str) {
        FILE *fp = (strcmp(opt_file_str, "-") == 0) ? stdin : fopen(opt_file_str, "r");
        if (!fp) {
            fprintf(stderr, "date: %s: %s\n", opt_file_str, strerror(errno));
            return 1;
        }
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = '\0';
            if (l == 0) continue;
            time_t file_time = parse_date_string(line, time(NULL));
            format_and_print_time(file_time, format_arg);
        }
        if (fp != stdin) fclose(fp);
        return 0;
    }

    if (opt_iso_8601) {
        if (strcasecmp(opt_iso_8601, "hours") == 0) format_arg = "%Y-%m-%dT%H%z";
        else if (strcasecmp(opt_iso_8601, "minutes") == 0) format_arg = "%Y-%m-%dT%H:%M%z";
        else if (strcasecmp(opt_iso_8601, "seconds") == 0) format_arg = "%Y-%m-%dT%H:%M:%S%z";
        else format_arg = "%Y-%m-%d";
    } else if (opt_rfc_822) {
        format_arg = "%a, %d %b %Y %H:%M:%S %z";
    } else if (opt_rfc_3339) {
        if (strcasecmp(opt_rfc_3339, "seconds") == 0) format_arg = "%Y-%m-%d %H:%M:%S%:z";
        else if (strcasecmp(opt_rfc_3339, "ns") == 0) format_arg = "%Y-%m-%d %H:%M:%S.000000000%:z";
        else format_arg = "%Y-%m-%d";
    }

    format_and_print_time(target_time, format_arg);
    return 0;
}
