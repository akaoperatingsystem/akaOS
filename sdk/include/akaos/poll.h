#ifndef AKAOS_POLL_H
#define AKAOS_POLL_H

#include <stdint.h>

#define AKAOS_POLLIN   INT16_C(0x0001)
#define AKAOS_POLLPRI  INT16_C(0x0002)
#define AKAOS_POLLOUT  INT16_C(0x0004)
#define AKAOS_POLLERR  INT16_C(0x0008)
#define AKAOS_POLLHUP  INT16_C(0x0010)
#define AKAOS_POLLNVAL INT16_C(0x0020)

#define AKAOS_POLL_MAX_FDS 64

struct akaos_pollfd {
    int32_t fd;
    int16_t events;
    int16_t revents;
};

#endif
