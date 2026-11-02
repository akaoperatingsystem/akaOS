#ifndef AKAOS_LIBC_TIME_H
#define AKAOS_LIBC_TIME_H

#include <sys/types.h>
#include <locale.h>
#include <stddef.h>

typedef int clockid_t;
#ifndef _CLOCK_T_DEFINED
#define _CLOCK_T_DEFINED
typedef long clock_t;
#endif
#define CLOCKS_PER_SEC ((clock_t)1000000)

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct itimerspec {
    struct timespec it_interval;
    struct timespec it_value;
};

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID 3

#ifdef __cplusplus
extern "C" {
#endif

int clock_gettime(int clock_id, struct timespec *time);
clock_t clock(void);
extern long timezone;
extern int daylight;
extern char *tzname[2];
void tzset(void);
int nanosleep(const struct timespec *requested, struct timespec *remaining);
time_t time(time_t *result);
double difftime(time_t end_time, time_t start_time);
int gettimeofday(struct timeval *value, void *timezone);
struct tm *gmtime(const time_t *clock);
struct tm *localtime(const time_t *clock);
struct tm *gmtime_r(const time_t *clock, struct tm *result);
struct tm *localtime_r(const time_t *clock, struct tm *result);
char *asctime(const struct tm *value);
char *asctime_r(const struct tm *value, char *buffer);
char *ctime(const time_t *clock);
char *ctime_r(const time_t *clock, char *buffer);
time_t mktime(struct tm *value);
size_t strftime(char *destination, size_t capacity, const char *format,
                const struct tm *value);
size_t strftime_l(char *destination, size_t capacity, const char *format,
                  const struct tm *value, locale_t locale);

#ifdef __cplusplus
}
#endif

#endif
