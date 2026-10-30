#ifndef _SYS_EPOLL_H
#define _SYS_EPOLL_H

#include <stdint.h>

/* Linux-compatible event bits used by GLib/GIO and Qt event dispatchers. */
#define EPOLLIN      UINT32_C(0x001)
#define EPOLLPRI     UINT32_C(0x002)
#define EPOLLOUT     UINT32_C(0x004)
#define EPOLLERR     UINT32_C(0x008)
#define EPOLLHUP     UINT32_C(0x010)
#define EPOLLRDHUP   UINT32_C(0x2000)
#define EPOLLET      UINT32_C(1U << 31)
#define EPOLLONESHOT UINT32_C(1U << 30)

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
#define EPOLL_CLOEXEC 0x80000

typedef union epoll_data {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;

struct epoll_event {
    uint32_t events;
    epoll_data_t data;
};

int epoll_create(int size);
int epoll_create1(int flags);
int epoll_ctl(int epoll_fd, int operation, int descriptor,
              const struct epoll_event *event);
int epoll_wait(int epoll_fd, struct epoll_event *events,
               int maxevents, int timeout);
int epoll_pwait(int epoll_fd, struct epoll_event *events,
                int maxevents, int timeout, const void *sigmask);

#endif
