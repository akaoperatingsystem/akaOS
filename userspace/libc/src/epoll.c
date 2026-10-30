#include <sys/epoll.h>
#include <errno.h>
#include <poll.h>
#include <stddef.h>

extern int __akaos_eventfd_descriptor_valid(int descriptor);
extern int __akaos_timerfd_descriptor_valid(int descriptor);

/* Keep libc-managed epoll handles outside XSCore's native descriptor range;
 * waits still use the kernel poll ABI for the watched descriptors. */
#define AKAOS_EPOLL_FD_BASE 1024
#define AKAOS_EPOLL_CAPACITY 4U
/* XSCore grows native descriptor tables to 256 entries. Keep epoll usable
 * across that range instead of retaining the old 16-fd bootstrap limit. */
#define AKAOS_NATIVE_FD_LIMIT 1024
#define AKAOS_EPOLL_WATCH_CAPACITY 64U

struct akaos_epoll_watch {
    int used;
    int descriptor;
    uint32_t events;
    epoll_data_t data;
};

struct akaos_epoll_object {
    int used;
    int descriptor;
    struct akaos_epoll_watch watches[AKAOS_EPOLL_WATCH_CAPACITY];
};

static struct akaos_epoll_object epoll_objects[AKAOS_EPOLL_CAPACITY];

static struct akaos_epoll_object *find_epoll(int descriptor) {
    if (descriptor < AKAOS_EPOLL_FD_BASE ||
        descriptor >= AKAOS_EPOLL_FD_BASE + (int)AKAOS_EPOLL_CAPACITY)
        return NULL;
    struct akaos_epoll_object *object =
        &epoll_objects[descriptor - AKAOS_EPOLL_FD_BASE];
    return object->used && object->descriptor == descriptor ? object : NULL;
}

/* Called by close(2) before it forwards a descriptor to the kernel. */
int __akaos_epoll_close(int descriptor) {
    struct akaos_epoll_object *object = find_epoll(descriptor);
    if (!object) return 0;
    object->used = 0;
    object->descriptor = -1;
    for (size_t index = 0; index < AKAOS_EPOLL_WATCH_CAPACITY; ++index)
        object->watches[index].used = 0;
    return 1;
}

int epoll_create1(int flags) {
    if (flags & ~EPOLL_CLOEXEC) {
        errno = EINVAL;
        return -1;
    }
    for (size_t index = 0; index < AKAOS_EPOLL_CAPACITY; ++index) {
        struct akaos_epoll_object *object = &epoll_objects[index];
        if (object->used) continue;
        object->used = 1;
        object->descriptor = AKAOS_EPOLL_FD_BASE + (int)index;
        for (size_t watch = 0; watch < AKAOS_EPOLL_WATCH_CAPACITY; ++watch)
            object->watches[watch].used = 0;
        return object->descriptor;
    }
    errno = EMFILE;
    return -1;
}

int epoll_create(int size) {
    if (size <= 0) {
        errno = EINVAL;
        return -1;
    }
    return epoll_create1(0);
}

static struct akaos_epoll_watch *find_watch(
    struct akaos_epoll_object *object, int descriptor) {
    for (size_t index = 0; index < AKAOS_EPOLL_WATCH_CAPACITY; ++index) {
        if (object->watches[index].used &&
            object->watches[index].descriptor == descriptor)
            return &object->watches[index];
    }
    return NULL;
}

int epoll_ctl(int epoll_fd, int operation, int descriptor,
              const struct epoll_event *event) {
    struct akaos_epoll_object *object = find_epoll(epoll_fd);
    if (!object || descriptor < 0 ||
        (descriptor >= AKAOS_NATIVE_FD_LIMIT &&
         !__akaos_eventfd_descriptor_valid(descriptor) &&
         !__akaos_timerfd_descriptor_valid(descriptor)) ||
        (operation != EPOLL_CTL_DEL && !event)) {
        errno = EINVAL;
        return -1;
    }
    struct akaos_epoll_watch *watch = find_watch(object, descriptor);
    if (operation == EPOLL_CTL_ADD) {
        if (watch) {
            errno = EEXIST;
            return -1;
        }
        for (size_t index = 0; index < AKAOS_EPOLL_WATCH_CAPACITY; ++index) {
            watch = &object->watches[index];
            if (watch->used) continue;
            watch->used = 1;
            watch->descriptor = descriptor;
            watch->events = event->events;
            watch->data = event->data;
            return 0;
        }
        /* The kernel descriptor table is the bounded resource here; expose
         * the standard "too many open files" error used for that limit. */
        errno = EMFILE;
        return -1;
    }
    if (operation == EPOLL_CTL_MOD) {
        if (!watch) {
            errno = ENOENT;
            return -1;
        }
        watch->events = event->events;
        watch->data = event->data;
        return 0;
    }
    if (operation == EPOLL_CTL_DEL) {
        if (!watch) {
            errno = ENOENT;
            return -1;
        }
        watch->used = 0;
        return 0;
    }
    errno = EINVAL;
    return -1;
}

static short poll_events(uint32_t events) {
    short result = 0;
    if (events & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) result |= POLLIN;
    if (events & EPOLLOUT) result |= POLLOUT;
    if (events & EPOLLERR) result |= POLLERR;
    return result;
}

int epoll_wait(int epoll_fd, struct epoll_event *events,
               int maxevents, int timeout) {
    struct akaos_epoll_object *object = find_epoll(epoll_fd);
    if (!object || !events || maxevents <= 0) {
        errno = EINVAL;
        return -1;
    }
    struct pollfd descriptors[AKAOS_EPOLL_WATCH_CAPACITY];
    struct akaos_epoll_watch *mapping[AKAOS_EPOLL_WATCH_CAPACITY];
    nfds_t count = 0;
    for (size_t index = 0; index < AKAOS_EPOLL_WATCH_CAPACITY; ++index) {
        struct akaos_epoll_watch *watch = &object->watches[index];
        if (!watch->used) continue;
        descriptors[count].fd = watch->descriptor;
        descriptors[count].events = poll_events(watch->events);
        descriptors[count].revents = 0;
        mapping[count] = watch;
        ++count;
    }
    int ready = poll(descriptors, count, timeout);
    if (ready <= 0) return ready;
    int result = 0;
    for (nfds_t index = 0; index < count && result < maxevents; ++index) {
        short revents = descriptors[index].revents;
        if (!revents) continue;
        struct akaos_epoll_watch *watch = mapping[index];
        uint32_t translated = watch->events & (EPOLLIN | EPOLLOUT | EPOLLPRI |
                                               EPOLLRDHUP | EPOLLET |
                                               EPOLLONESHOT);
        if (revents & (POLLERR | POLLNVAL)) translated |= EPOLLERR;
        if (revents & (POLLHUP | POLLNVAL)) translated |= EPOLLHUP;
        events[result].events = translated;
        events[result].data = watch->data;
        ++result;
        if (watch->events & EPOLLONESHOT) watch->used = 0;
    }
    return result;
}

int epoll_pwait(int epoll_fd, struct epoll_event *events,
                int maxevents, int timeout, const void *sigmask) {
    (void)sigmask;
    return epoll_wait(epoll_fd, events, maxevents, timeout);
}
