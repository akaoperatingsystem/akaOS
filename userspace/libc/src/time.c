#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <utime.h>
#include <unistd.h>

long timezone;
int daylight;
static char akaos_tz_standard[16] = "UTC";
static char akaos_tz_daylight[16] = "UTC";
char *tzname[2] = {akaos_tz_standard, akaos_tz_daylight};

static size_t copy_tz_name(char *destination, size_t capacity,
                           const char **cursor) {
    size_t length = 0U;
    const char *source = *cursor;
    int bracketed = *source == '<';
    if (bracketed) ++source;
    while (*source && length + 1U < capacity &&
           (bracketed ? *source != '>' :
            ((*source >= 'A' && *source <= 'Z') ||
             (*source >= 'a' && *source <= 'z')))) {
        destination[length++] = *source++;
    }
    if (bracketed && *source != '>') return 0U;
    if (bracketed) ++source;
    if (length < 3U) return 0U;
    destination[length] = '\0';
    *cursor = source;
    return length;
}

void tzset(void) {
    const char *setting = getenv("TZ");
    if (!setting || !*setting) setting = "UTC0";
    const char *cursor = setting;
    char standard[sizeof(akaos_tz_standard)];
    if (copy_tz_name(standard, sizeof(standard), &cursor) == 0U) {
        timezone = 0;
        daylight = 0;
        memcpy(akaos_tz_standard, "UTC", 4U);
        memcpy(akaos_tz_daylight, "UTC", 4U);
        return;
    }

    int sign = 1;
    if (*cursor == '-') { sign = -1; ++cursor; }
    else if (*cursor == '+') ++cursor;
    int hours = 0, minutes = 0, seconds = 0;
    int digits = 0;
    while (*cursor >= '0' && *cursor <= '9' && digits < 3) {
        hours = hours * 10 + (*cursor++ - '0');
        ++digits;
    }
    if (digits == 0) hours = 0;
    if (*cursor == ':') {
        ++cursor;
        if (*cursor < '0' || *cursor > '9') goto invalid_timezone;
        minutes = (*cursor++ - '0');
        if (*cursor >= '0' && *cursor <= '9')
            minutes = minutes * 10 + (*cursor++ - '0');
        if (*cursor == ':') {
            ++cursor;
            if (*cursor < '0' || *cursor > '9') goto invalid_timezone;
            seconds = (*cursor++ - '0');
            if (*cursor >= '0' && *cursor <= '9')
                seconds = seconds * 10 + (*cursor++ - '0');
        }
    }
    if (minutes > 59 || seconds > 59 || hours > 24) goto invalid_timezone;

    timezone = (long)sign *
        ((long)hours * 3600L + (long)minutes * 60L + seconds);
    daylight = 0;
    memcpy(akaos_tz_standard, standard, strlen(standard) + 1U);
    memcpy(akaos_tz_daylight, standard, strlen(standard) + 1U);
    if (*cursor) {
        const char *daylight_cursor = cursor;
        char daylight_name[sizeof(akaos_tz_daylight)];
        if (copy_tz_name(daylight_name, sizeof(daylight_name),
                         &daylight_cursor) != 0U) {
            memcpy(akaos_tz_daylight, daylight_name,
                   strlen(daylight_name) + 1U);
            /* A daylight abbreviation without transition rules is not
             * enough to determine DST for a date, so remain on standard time. */
        }
    }
    tzname[0] = akaos_tz_standard;
    tzname[1] = akaos_tz_daylight;
    return;

invalid_timezone:
    timezone = 0;
    daylight = 0;
    memcpy(akaos_tz_standard, "UTC", 4U);
    memcpy(akaos_tz_daylight, "UTC", 4U);
    tzname[0] = akaos_tz_standard;
    tzname[1] = akaos_tz_daylight;
}

/* The kernel publishes wall-clock seconds and monotonic uptime through the
 * read-only proc files.  Keeping the conversion here gives desktop libraries
 * the normal POSIX clock API without adding an architecture-specific syscall;
 * the proc implementation is already backed by every akaOS port's native
 * timer/RTC source. */
static int read_clock_file(const char *path, uint64_t *seconds,
                           uint32_t *nanoseconds) {
    if (!path || !seconds || !nanoseconds) {
        errno = EFAULT;
        return -1;
    }
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) return -1;
    char text[64];
    ssize_t count = read(descriptor, text, sizeof(text) - 1);
    int saved_errno = errno;
    (void)close(descriptor);
    if (count <= 0) {
        errno = count < 0 ? saved_errno : EIO;
        return -1;
    }
    text[count] = '\0';

    uint64_t whole = 0;
    size_t index = 0;
    while (text[index] >= '0' && text[index] <= '9') {
        uint32_t digit = (uint32_t)(text[index] - '0');
        if (whole > UINT64_MAX / 10U ||
            (whole == UINT64_MAX / 10U && digit > UINT64_MAX % 10U)) {
            errno = ERANGE;
            return -1;
        }
        whole = whole * 10U + digit;
        ++index;
    }
    uint32_t fraction = 0;
    uint32_t digits = 0;
    if (text[index] == '.') {
        ++index;
        while (text[index] >= '0' && text[index] <= '9') {
            if (digits < 9U) {
                fraction = fraction * 10U + (uint32_t)(text[index] - '0');
                ++digits;
            }
            ++index;
        }
    }
    while (digits < 9U) {
        fraction *= 10U;
        ++digits;
    }
    *seconds = whole;
    *nanoseconds = fraction;
    return 0;
}

int clock_gettime(int clock_id, struct timespec *result) {
    if (!result || (clock_id != CLOCK_REALTIME &&
                    clock_id != CLOCK_MONOTONIC)) {
        errno = EINVAL;
        return -1;
    }
    uint64_t seconds = 0;
    uint32_t nanoseconds = 0;
    const char *path = clock_id == CLOCK_REALTIME
                           ? "/proc/realtime" : "/proc/uptime";
    if (read_clock_file(path, &seconds, &nanoseconds) < 0) return -1;
    result->tv_sec = (time_t)seconds;
    result->tv_nsec = (long)nanoseconds;
    return 0;
}

time_t time(time_t *result) {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) < 0) return (time_t)-1;
    if (result) *result = now.tv_sec;
    return now.tv_sec;
}

double difftime(time_t end_time, time_t start_time) {
    return (double)end_time - (double)start_time;
}

int gettimeofday(struct timeval *value, void *timezone) {
    (void)timezone;
    if (!value) {
        errno = EFAULT;
        return -1;
    }
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) < 0) return -1;
    value->tv_sec = now.tv_sec;
    value->tv_usec = now.tv_nsec / 1000L;
    return 0;
}

int futimens(int descriptor, const struct timespec times[2]) {
    (void)descriptor;
    (void)times;
    /* Like utimes, unavailable until the VFS has a timestamp update syscall. */
    errno = ENOSYS;
    return -1;
}

int utimes(const char *path, const struct timeval times[2]) {
    (void)path;
    (void)times;
    /* The current XSCore VFS exposes timestamps for stat but has no metadata
     * update syscall yet. Report that capability gap instead of pretending
     * the requested timestamps were applied. */
    errno = ENOSYS;
    return -1;
}

int utime(const char *path, const struct utimbuf *times) {
    struct timeval converted[2];
    if (times) {
        converted[0].tv_sec = times->actime;
        converted[0].tv_usec = 0;
        converted[1].tv_sec = times->modtime;
        converted[1].tv_usec = 0;
    } else {
        struct timespec now;
        if (clock_gettime(CLOCK_REALTIME, &now) < 0) return -1;
        converted[0].tv_sec = now.tv_sec;
        converted[0].tv_usec = now.tv_nsec / 1000L;
        converted[1] = converted[0];
    }
    return utimes(path, converted);
}

int nanosleep(const struct timespec *requested, struct timespec *remaining) {
    if (!requested || requested->tv_sec < 0 || requested->tv_nsec < 0 ||
        requested->tv_nsec >= 1000000000L) {
        errno = EINVAL;
        return -1;
    }
    if (remaining) {
        remaining->tv_sec = 0;
        remaining->tv_nsec = 0;
    }

    /* poll(2) accepts millisecond timeouts.  Break long requests into one
     * second slices so a 32-bit target cannot overflow the timeout argument. */
    time_t seconds = requested->tv_sec;
    while (seconds > 0) {
        int result = poll(NULL, 0, 1000);
        if (result < 0) return -1;
        --seconds;
    }
    if (requested->tv_nsec != 0) {
        int milliseconds = (int)((requested->tv_nsec + 999999L) / 1000000L);
        if (poll(NULL, 0, milliseconds) < 0) return -1;
    }
    return 0;
}

static int64_t floor_divide(int64_t value, int64_t divisor) {
    int64_t quotient = value / divisor;
    int64_t remainder = value % divisor;
    if (remainder < 0) --quotient;
    return quotient;
}

static int is_leap_year(int64_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int64_t civil_days_from_epoch(int64_t year, unsigned int month,
                                     unsigned int day) {
    year -= month <= 2U;
    int64_t era = floor_divide(year, 400);
    unsigned int year_of_era = (unsigned int)(year - era * 400);
    unsigned int shifted_month = month > 2U ? month - 3U : month + 9U;
    unsigned int day_of_year = (153U * shifted_month + 2U) / 5U + day - 1U;
    unsigned int day_of_era = year_of_era * 365U + year_of_era / 4U -
                              year_of_era / 100U + day_of_year;
    return era * 146097 + (int64_t)day_of_era - 719468;
}

struct tm *gmtime_r(const time_t *clock, struct tm *result) {
    if (!clock || !result) {
        errno = EFAULT;
        return NULL;
    }
    int64_t seconds = (int64_t)*clock;
    int64_t days = floor_divide(seconds, 86400);
    int64_t day_seconds = seconds - days * 86400;
    int64_t shifted_days = days + 719468;
    int64_t era = floor_divide(shifted_days, 146097);
    unsigned int day_of_era =
        (unsigned int)(shifted_days - era * 146097);
    unsigned int year_of_era =
        (day_of_era - day_of_era / 1460U + day_of_era / 36524U -
         day_of_era / 146096U) / 365U;
    int64_t year = (int64_t)year_of_era + era * 400;
    unsigned int day_of_year = day_of_era -
        (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
    unsigned int month_prime = (5U * day_of_year + 2U) / 153U;
    unsigned int day = day_of_year - (153U * month_prime + 2U) / 5U + 1U;
    unsigned int month = month_prime < 10U ? month_prime + 3U :
                                                 month_prime - 9U;
    year += month <= 2U;
    if (year - 1900 < INT_MIN || year - 1900 > INT_MAX) {
        errno = ERANGE;
        return NULL;
    }

    static const unsigned short days_before_month[2][12] = {
        {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
        {0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335}
    };
    result->tm_sec = (int)(day_seconds % 60);
    result->tm_min = (int)((day_seconds / 60) % 60);
    result->tm_hour = (int)(day_seconds / 3600);
    result->tm_mday = (int)day;
    result->tm_mon = (int)month - 1;
    result->tm_year = (int)(year - 1900);
    result->tm_wday = (int)((days + 4) % 7);
    if (result->tm_wday < 0) result->tm_wday += 7;
    result->tm_yday = days_before_month[is_leap_year(year)][month - 1U] +
                      (int)day - 1;
    result->tm_isdst = 0;
    result->tm_gmtoff = 0;
    result->tm_zone = "UTC";
    return result;
}

struct tm *localtime_r(const time_t *clock, struct tm *result) {
    if (!clock || !result) {
        errno = EFAULT;
        return NULL;
    }
    tzset();
    int64_t local_seconds = (int64_t)*clock - (int64_t)timezone;
    time_t local_clock = (time_t)local_seconds;
    if ((int64_t)local_clock != local_seconds) {
        errno = ERANGE;
        return NULL;
    }
    if (!gmtime_r(&local_clock, result)) return NULL;
    result->tm_gmtoff = -timezone;
    result->tm_zone = tzname[0];
    return result;
}

static __thread struct tm akaos_calendar_result;

struct tm *gmtime(const time_t *clock) {
    return gmtime_r(clock, &akaos_calendar_result);
}

struct tm *localtime(const time_t *clock) {
    return localtime_r(clock, &akaos_calendar_result);
}

char *asctime_r(const struct tm *value, char *buffer) {
    static const char *const weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *const months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    if (!value || !buffer) {
        errno = EFAULT;
        return NULL;
    }
    if (value->tm_wday < 0 || value->tm_wday >= 7 ||
        value->tm_mon < 0 || value->tm_mon >= 12 ||
        value->tm_mday < 1 || value->tm_mday > 31 ||
        value->tm_hour < 0 || value->tm_hour > 23 ||
        value->tm_min < 0 || value->tm_min > 59 ||
        value->tm_sec < 0 || value->tm_sec > 60) {
        errno = EINVAL;
        return NULL;
    }
    int64_t year = (int64_t)value->tm_year + 1900;
    if (year < 0 || year > 9999) {
        errno = ERANGE;
        return NULL;
    }

    memcpy(buffer, weekdays[value->tm_wday], 3U);
    buffer[3] = ' ';
    memcpy(buffer + 4, months[value->tm_mon], 3U);
    buffer[7] = ' ';
    buffer[8] = value->tm_mday < 10
                     ? ' ' : (char)('0' + value->tm_mday / 10);
    buffer[9] = (char)('0' + value->tm_mday % 10);
    buffer[10] = ' ';
    buffer[11] = (char)('0' + value->tm_hour / 10);
    buffer[12] = (char)('0' + value->tm_hour % 10);
    buffer[13] = ':';
    buffer[14] = (char)('0' + value->tm_min / 10);
    buffer[15] = (char)('0' + value->tm_min % 10);
    buffer[16] = ':';
    buffer[17] = (char)('0' + value->tm_sec / 10);
    buffer[18] = (char)('0' + value->tm_sec % 10);
    buffer[19] = ' ';
    buffer[20] = (char)('0' + (year / 1000) % 10);
    buffer[21] = (char)('0' + (year / 100) % 10);
    buffer[22] = (char)('0' + (year / 10) % 10);
    buffer[23] = (char)('0' + year % 10);
    buffer[24] = '\n';
    buffer[25] = '\0';
    return buffer;
}

char *asctime(const struct tm *value) {
    static __thread char buffer[26];
    return asctime_r(value, buffer);
}

char *ctime_r(const time_t *clock, char *buffer) {
    if (!clock || !buffer) {
        errno = EFAULT;
        return NULL;
    }
    struct tm broken_down;
    if (!localtime_r(clock, &broken_down)) return NULL;
    return asctime_r(&broken_down, buffer);
}

char *ctime(const time_t *clock) {
    if (!clock) {
        errno = EFAULT;
        return NULL;
    }
    static __thread char buffer[26];
    struct tm *broken_down = localtime(clock);
    if (!broken_down) return NULL;
    return asctime_r(broken_down, buffer);
}

static int64_t normalize_component(int64_t value, int64_t divisor,
                                   int64_t *remainder) {
    int64_t quotient = floor_divide(value, divisor);
    *remainder = value - quotient * divisor;
    return quotient;
}

time_t mktime(struct tm *value) {
    if (!value) {
        errno = EFAULT;
        return (time_t)-1;
    }
    int64_t year = (int64_t)value->tm_year + 1900;
    int64_t month = value->tm_mon;
    year += floor_divide(month, 12);
    month -= floor_divide(month, 12) * 12;

    int64_t seconds = value->tm_sec;
    int64_t minutes = value->tm_min;
    int64_t hours = value->tm_hour;
    int64_t days = (int64_t)value->tm_mday - 1;
    days += normalize_component(seconds, 60, &seconds);
    minutes += seconds / 60;
    seconds %= 60;
    days += normalize_component(minutes, 60, &minutes);
    hours += minutes / 60;
    minutes %= 60;
    days += normalize_component(hours, 24, &hours);
    hours %= 24;

    int64_t epoch_days = civil_days_from_epoch(year, (unsigned int)month + 1U, 1U) + days;
    if (epoch_days > INT64_MAX / 86400 || epoch_days < INT64_MIN / 86400) {
        errno = ERANGE;
        return (time_t)-1;
    }
    int64_t epoch_seconds = epoch_days * 86400 + hours * 3600 +
                            minutes * 60 + seconds;
    time_t local_converted = (time_t)epoch_seconds;
    if ((int64_t)local_converted != epoch_seconds) {
        errno = ERANGE;
        return (time_t)-1;
    }
    if (!gmtime_r(&local_converted, value)) return (time_t)-1;
    tzset();
    int64_t utc_seconds = epoch_seconds + (int64_t)timezone;
    time_t converted = (time_t)utc_seconds;
    if ((int64_t)converted != utc_seconds) {
        errno = ERANGE;
        return (time_t)-1;
    }
    value->tm_gmtoff = -timezone;
    value->tm_zone = tzname[0];
    return converted;
}

static int append_text(char *destination, size_t capacity, size_t *length,
                       const char *text) {
    size_t index = 0U;
    while (text[index]) {
        if (*length + 1U >= capacity) return 0;
        destination[(*length)++] = text[index++];
    }
    return 1;
}

static int append_format(char *destination, size_t capacity, size_t *length,
                         const char *format, int value) {
    char buffer[48];
    int count = snprintf(buffer, sizeof(buffer), format, value);
    if (count < 0 || (size_t)count >= sizeof(buffer)) return 0;
    return append_text(destination, capacity, length, buffer);
}

size_t strftime_l(char *destination, size_t capacity, const char *format,
                  const struct tm *value, locale_t locale) {
    (void)locale;
    if (!destination || !format || !value || capacity == 0U) return 0U;
    static const char *const weekdays_short[] =
        {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *const weekdays_long[] =
        {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *const months_short[] =
        {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *const months_long[] =
        {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    size_t length = 0U;
    for (size_t index = 0U; format[index]; ++index) {
        if (format[index] != '%') {
            char literal[2] = {format[index], '\0'};
            if (!append_text(destination, capacity, &length, literal)) return 0U;
            continue;
        }
        char conversion = format[++index];
        if (!conversion) break;
        if (conversion == 'E' || conversion == 'O') conversion = format[++index];
        char buffer[64];
        switch (conversion) {
            case '%': if (!append_text(destination, capacity, &length, "%")) return 0U; break;
            case 'a': {
                if (value->tm_wday < 0 || value->tm_wday > 6 ||
                    !append_text(destination, capacity, &length,
                                 weekdays_short[value->tm_wday])) return 0U;
                break;
            }
            case 'A': {
                if (value->tm_wday < 0 || value->tm_wday > 6 ||
                    !append_text(destination, capacity, &length,
                                 weekdays_long[value->tm_wday])) return 0U;
                break;
            }
            case 'b': case 'h':
                if (value->tm_mon < 0 || value->tm_mon > 11 ||
                    !append_text(destination, capacity, &length, months_short[value->tm_mon])) return 0U;
                break;
            case 'B':
                if (value->tm_mon < 0 || value->tm_mon > 11 ||
                    !append_text(destination, capacity, &length, months_long[value->tm_mon])) return 0U;
                break;
            case 'c':
                if (value->tm_wday < 0 || value->tm_wday > 6 || value->tm_mon < 0 || value->tm_mon > 11) return 0U;
                if (snprintf(buffer, sizeof(buffer), "%s %s %2d %02d:%02d:%02d %04d",
                    weekdays_short[value->tm_wday], months_short[value->tm_mon],
                    value->tm_mday, value->tm_hour, value->tm_min, value->tm_sec,
                    value->tm_year + 1900) < 0 ||
                    !append_text(destination, capacity, &length, buffer)) return 0U;
                break;
            case 'C': if (!append_format(destination, capacity, &length, "%02d", (value->tm_year + 1900) / 100)) return 0U; break;
            case 'd': if (!append_format(destination, capacity, &length, "%02d", value->tm_mday)) return 0U; break;
            case 'D':
                if (!append_format(destination, capacity, &length, "%02d", value->tm_mon + 1) ||
                    !append_text(destination, capacity, &length, "/") ||
                    !append_format(destination, capacity, &length, "%02d", value->tm_mday) ||
                    !append_text(destination, capacity, &length, "/") ||
                    !append_format(destination, capacity, &length, "%02d", (value->tm_year + 1900) % 100)) return 0U;
                break;
            case 'e': if (!append_format(destination, capacity, &length, "%2d", value->tm_mday)) return 0U; break;
            case 'F':
                if (!append_format(destination, capacity, &length, "%04d", value->tm_year + 1900) ||
                    !append_text(destination, capacity, &length, "-") ||
                    !append_format(destination, capacity, &length, "%02d", value->tm_mon + 1) ||
                    !append_text(destination, capacity, &length, "-") ||
                    !append_format(destination, capacity, &length, "%02d", value->tm_mday)) return 0U;
                break;
            case 'H': if (!append_format(destination, capacity, &length, "%02d", value->tm_hour)) return 0U; break;
            case 'I': { int hour = value->tm_hour % 12; if (hour == 0) hour = 12;
                if (!append_format(destination, capacity, &length, "%02d", hour)) return 0U; break; }
            case 'j': if (!append_format(destination, capacity, &length, "%03d", value->tm_yday + 1)) return 0U; break;
            case 'm': if (!append_format(destination, capacity, &length, "%02d", value->tm_mon + 1)) return 0U; break;
            case 'M': if (!append_format(destination, capacity, &length, "%02d", value->tm_min)) return 0U; break;
            case 'n': if (!append_text(destination, capacity, &length, "\n")) return 0U; break;
            case 'p': if (!append_text(destination, capacity, &length, value->tm_hour < 12 ? "AM" : "PM")) return 0U; break;
            case 'r':
                if (snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d %s",
                    (value->tm_hour % 12) ? value->tm_hour % 12 : 12,
                    value->tm_min, value->tm_sec,
                    value->tm_hour < 12 ? "AM" : "PM") < 0 ||
                    !append_text(destination, capacity, &length, buffer)) return 0U;
                break;
            case 'R':
                if (!append_format(destination, capacity, &length, "%02d", value->tm_hour) ||
                    !append_text(destination, capacity, &length, ":") ||
                    !append_format(destination, capacity, &length, "%02d", value->tm_min)) return 0U;
                break;
            case 'S': if (!append_format(destination, capacity, &length, "%02d", value->tm_sec)) return 0U; break;
            case 't': if (!append_text(destination, capacity, &length, "\t")) return 0U; break;
            case 'T':
                if (!append_format(destination, capacity, &length, "%02d", value->tm_hour) ||
                    !append_text(destination, capacity, &length, ":") ||
                    !append_format(destination, capacity, &length, "%02d", value->tm_min) ||
                    !append_text(destination, capacity, &length, ":") ||
                    !append_format(destination, capacity, &length, "%02d", value->tm_sec)) return 0U;
                break;
            case 'u': if (!append_format(destination, capacity, &length, "%d", value->tm_wday == 0 ? 7 : value->tm_wday)) return 0U; break;
            case 'w': if (!append_format(destination, capacity, &length, "%d", value->tm_wday)) return 0U; break;
            case 'x':
                if (!append_format(destination, capacity, &length, "%02d", value->tm_mon + 1) ||
                    !append_text(destination, capacity, &length, "/") ||
                    !append_format(destination, capacity, &length, "%02d", value->tm_mday) ||
                    !append_text(destination, capacity, &length, "/") ||
                    !append_format(destination, capacity, &length, "%02d", (value->tm_year + 1900) % 100)) return 0U;
                break;
            case 'X':
                if (!append_format(destination, capacity, &length, "%02d", value->tm_hour) ||
                    !append_text(destination, capacity, &length, ":") ||
                    !append_format(destination, capacity, &length, "%02d", value->tm_min) ||
                    !append_text(destination, capacity, &length, ":") ||
                    !append_format(destination, capacity, &length, "%02d", value->tm_sec)) return 0U;
                break;
            case 'y': if (!append_format(destination, capacity, &length, "%02d", (value->tm_year + 1900) % 100)) return 0U; break;
            case 'Y': if (!append_format(destination, capacity, &length, "%04d", value->tm_year + 1900)) return 0U; break;
            case 'z': if (!append_text(destination, capacity, &length, "+0000")) return 0U; break;
            case 'Z': if (!append_text(destination, capacity, &length, value->tm_zone ? value->tm_zone : "UTC")) return 0U; break;
            default:
                if (!append_text(destination, capacity, &length, "%") ||
                    !append_text(destination, capacity, &length, (char[2]){conversion, '\0'})) return 0U;
                break;
        }
    }
    destination[length] = '\0';
    return length;
}

size_t strftime(char *destination, size_t capacity, const char *format,
                const struct tm *value) {
    return strftime_l(destination, capacity, format, value, LC_GLOBAL_LOCALE);
}

clock_t clock(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0 ||
        clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (clock_t)ts.tv_sec * 1000000 + (clock_t)(ts.tv_nsec / 1000);
    }
    return (clock_t)-1;
}

#include <sys/times.h>
clock_t times(struct tms *buf) {
    if (buf) {
        memset(buf, 0, sizeof(*buf));
    }
    return clock();
}
