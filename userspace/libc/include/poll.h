#ifndef _POLL_H
#define _POLL_H

#include <akaos/poll.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>

typedef unsigned long nfds_t;

#define POLLIN   AKAOS_POLLIN
#define POLLPRI  AKAOS_POLLPRI
#define POLLOUT  AKAOS_POLLOUT
#define POLLERR  AKAOS_POLLERR
#define POLLHUP  AKAOS_POLLHUP
#define POLLNVAL AKAOS_POLLNVAL

#define POLLRDNORM POLLIN
#define POLLWRNORM POLLOUT

struct pollfd {
    int fd;
    short events;
    short revents;
};

#ifdef __cplusplus
extern "C" {
#endif

int poll(struct pollfd *fds, nfds_t count, int timeout);
int ppoll(struct pollfd *fds, nfds_t count, const struct timespec *timeout,
          const sigset_t *signal_mask);

#ifdef __cplusplus
}
#endif

#endif
