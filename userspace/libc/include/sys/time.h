#ifndef AKAOS_LIBC_SYS_TIME_H
#define AKAOS_LIBC_SYS_TIME_H

#include <time.h>

#ifndef timercmp
#define timercmp(left, right, operation) \
    (((left)->tv_sec == (right)->tv_sec) \
        ? ((left)->tv_usec operation (right)->tv_usec) \
        : ((left)->tv_sec operation (right)->tv_sec))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* timeval is shared with time.h in the current libc ABI. */
int utimes(const char *path, const struct timeval times[2]);

#ifdef __cplusplus
}
#endif

#endif
