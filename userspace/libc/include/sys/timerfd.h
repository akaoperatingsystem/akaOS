#ifndef AKAOS_LIBC_SYS_TIMERFD_H
#define AKAOS_LIBC_SYS_TIMERFD_H

#include <time.h>

/* Linux-compatible timerfd flags used by GLib/GIO and event loops.  The
 * descriptor itself is implemented by libc over akaOS' native pipe/poll
 * path, so the values intentionally match the usual Linux ABI. */
#define TFD_CLOEXEC       0x80000
#define TFD_NONBLOCK      0x800
#define TFD_TIMER_ABSTIME 1

#ifdef __cplusplus
extern "C" {
#endif

int timerfd_create(int clock_id, int flags);
int timerfd_settime(int descriptor, int flags,
                    const struct itimerspec *new_value,
                    struct itimerspec *old_value);
int timerfd_gettime(int descriptor, struct itimerspec *current_value);

#ifdef __cplusplus
}
#endif

#endif
