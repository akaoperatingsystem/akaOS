#include <sys/eventfd.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

/* Keep fake descriptors outside the kernel's 0..255 descriptor namespace.  A
 * pipe pair backs each object; only its read side is exposed to callers and
 * the poll wrapper maps the fake number back to that native descriptor. */
#define AKAOS_EVENTFD_BASE 1032
#define AKAOS_EVENTFD_CAPACITY 32U
#define AKAOS_EVENTFD_DUP_BASE 1064
#define AKAOS_EVENTFD_DUP_CAPACITY 16U
#define EVENTFD_MAX UINT64_C(0xfffffffffffffffe)

struct akaos_eventfd_object {
    int used;
    int descriptor;
    int read_fd;
    int write_fd;
    int flags;
    uint32_t descriptor_flags;
    uint32_t descriptor_references;
    uint64_t value;
};

struct akaos_eventfd_alias {
    int used;
    int descriptor;
    uint32_t descriptor_flags;
    struct akaos_eventfd_object *object;
};

static struct akaos_eventfd_object objects[AKAOS_EVENTFD_CAPACITY];
static struct akaos_eventfd_alias aliases[AKAOS_EVENTFD_DUP_CAPACITY];

static struct akaos_eventfd_alias *find_eventfd_alias(int descriptor) {
    if (descriptor < AKAOS_EVENTFD_DUP_BASE ||
        descriptor >= AKAOS_EVENTFD_DUP_BASE +
                         (int)AKAOS_EVENTFD_DUP_CAPACITY) return NULL;
    struct akaos_eventfd_alias *alias =
        &aliases[descriptor - AKAOS_EVENTFD_DUP_BASE];
    return alias->used && alias->descriptor == descriptor && alias->object &&
                   alias->object->used
               ? alias : NULL;
}

static struct akaos_eventfd_object *find_eventfd(int descriptor) {
    if (descriptor >= AKAOS_EVENTFD_BASE &&
        descriptor < AKAOS_EVENTFD_BASE + (int)AKAOS_EVENTFD_CAPACITY) {
        struct akaos_eventfd_object *object =
            &objects[descriptor - AKAOS_EVENTFD_BASE];
        if (object->used && object->descriptor == descriptor) return object;
    }
    struct akaos_eventfd_alias *alias = find_eventfd_alias(descriptor);
    return alias ? alias->object : NULL;
}

static int eventfd_is_used(int descriptor) {
    return find_eventfd(descriptor) != NULL;
}

static int eventfd_signal(struct akaos_eventfd_object *object) {
    uint8_t marker = 1;
    return write(object->write_fd, &marker, sizeof(marker)) ==
           (ssize_t)sizeof(marker);
}

int eventfd(unsigned int initval, int flags) {
    if (flags & ~(EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE)) {
        errno = EINVAL;
        return -1;
    }
    for (size_t index = 0; index < AKAOS_EVENTFD_CAPACITY; ++index) {
        struct akaos_eventfd_object *object = &objects[index];
        if (object->used) continue;
        int descriptors[2];
        if (pipe(descriptors) < 0) return -1;
        object->used = 1;
        object->descriptor = AKAOS_EVENTFD_BASE + (int)index;
        object->read_fd = descriptors[0];
        object->write_fd = descriptors[1];
        object->flags = flags;
        object->descriptor_flags =
            (flags & EFD_CLOEXEC) ? FD_CLOEXEC : 0U;
        object->descriptor_references = 1U;
        object->value = initval;
        if (initval != 0 && !eventfd_signal(object)) {
            close(object->read_fd);
            close(object->write_fd);
            object->used = 0;
            object->descriptor = -1;
            object->descriptor_references = 0U;
            errno = EIO;
            return -1;
        }
        return object->descriptor;
    }
    errno = EMFILE;
    return -1;
}

/* Returns 1 when descriptor was consumed by this bridge, 0 otherwise. */
int __akaos_eventfd_read(int descriptor, void *buffer, size_t length,
                         ssize_t *result) {
    struct akaos_eventfd_object *object = find_eventfd(descriptor);
    if (!object) return 0;
    if (!buffer || length < sizeof(uint64_t)) {
        errno = EINVAL;
        *result = -1;
        return 1;
    }
    if (object->value == 0 && (object->flags & EFD_NONBLOCK)) {
        errno = EAGAIN;
        *result = -1;
        return 1;
    }
    uint8_t marker;
    ssize_t consumed = read(object->read_fd, &marker, sizeof(marker));
    if (consumed < 0) {
        *result = -1;
        return 1;
    }
    uint64_t value = object->value;
    if (object->flags & EFD_SEMAPHORE) {
        value = 1;
        if (object->value > 0) --object->value;
    } else {
        object->value = 0;
    }
    if (object->value != 0) (void)eventfd_signal(object);
    *(uint64_t *)buffer = value;
    *result = sizeof(uint64_t);
    return 1;
}

int __akaos_eventfd_write(int descriptor, const void *buffer, size_t length,
                          ssize_t *result) {
    struct akaos_eventfd_object *object = find_eventfd(descriptor);
    if (!object) return 0;
    if (!buffer || length != sizeof(uint64_t)) {
        errno = EINVAL;
        *result = -1;
        return 1;
    }
    uint64_t amount = *(const uint64_t *)buffer;
    if (amount == UINT64_MAX || object->value > EVENTFD_MAX - amount) {
        errno = EINVAL;
        *result = -1;
        return 1;
    }
    int was_zero = object->value == 0;
    object->value += amount;
    if (was_zero && object->value != 0 && !eventfd_signal(object)) {
        object->value -= amount;
        errno = EIO;
        *result = -1;
        return 1;
    }
    *result = sizeof(uint64_t);
    return 1;
}

/* Used by poll(2)/epoll(7) to turn the virtual descriptor into a native
 * descriptor while preserving POLLIN/POLLOUT direction. */
int __akaos_eventfd_poll_fd(int descriptor, short events) {
    struct akaos_eventfd_object *object = find_eventfd(descriptor);
    if (!object) return descriptor;
    return (events & POLLOUT) ? object->write_fd : object->read_fd;
}

int __akaos_eventfd_descriptor_valid(int descriptor) {
    return eventfd_is_used(descriptor);
}

int __akaos_eventfd_fcntl(int descriptor, int command, uint64_t argument,
                          int *result) {
    struct akaos_eventfd_object *object = find_eventfd(descriptor);
    if (!object) return 0;
    struct akaos_eventfd_alias *alias = find_eventfd_alias(descriptor);
    uint32_t *descriptor_flags = alias ? &alias->descriptor_flags
                                       : &object->descriptor_flags;
    if (command == F_GETFD) {
        *result = (int)*descriptor_flags;
        return 1;
    }
    if (command == F_SETFD) {
        if (argument & ~(uint64_t)FD_CLOEXEC) {
            errno = EINVAL;
            *result = -1;
            return 1;
        }
        *descriptor_flags = (uint32_t)argument;
        *result = 0;
        return 1;
    }
    if (command == F_GETFL) {
        *result = O_RDWR | ((object->flags & EFD_NONBLOCK) ? O_NONBLOCK : 0);
        return 1;
    }
    if (command == F_SETFL) {
        if (argument & O_NONBLOCK) object->flags |= EFD_NONBLOCK;
        else object->flags &= ~EFD_NONBLOCK;
        *result = 0;
        return 1;
    }
    if (command != F_DUPFD && command != F_DUPFD_CLOEXEC) return 0;
    if (argument > INT32_MAX) {
        errno = EINVAL;
        *result = -1;
        return 1;
    }
    for (size_t index = 0; index < AKAOS_EVENTFD_DUP_CAPACITY; ++index) {
        int candidate = AKAOS_EVENTFD_DUP_BASE + (int)index;
        struct akaos_eventfd_alias *slot = &aliases[index];
        if ((uint64_t)candidate < argument || slot->used) continue;
        slot->used = 1;
        slot->descriptor = candidate;
        slot->descriptor_flags = command == F_DUPFD_CLOEXEC
            ? FD_CLOEXEC : 0U;
        slot->object = object;
        ++object->descriptor_references;
        *result = candidate;
        return 1;
    }
    errno = EMFILE;
    *result = -1;
    return 1;
}

/* Called by close(2) before it reaches the kernel. */
int __akaos_eventfd_close(int descriptor) {
    struct akaos_eventfd_object *object = find_eventfd(descriptor);
    if (!object) return 0;
    struct akaos_eventfd_alias *alias = find_eventfd_alias(descriptor);
    if (alias) {
        alias->used = 0;
        alias->descriptor = -1;
        alias->descriptor_flags = 0U;
        alias->object = NULL;
    } else {
        object->descriptor = -1;
        object->descriptor_flags = 0U;
    }
    if (object->descriptor_references > 0U)
        --object->descriptor_references;
    if (object->descriptor_references == 0U) {
        int read_fd = object->read_fd;
        int write_fd = object->write_fd;
        object->used = 0;
        object->read_fd = -1;
        object->write_fd = -1;
        object->flags = 0;
        object->value = 0;
        (void)close(read_fd);
        (void)close(write_fd);
    }
    return 1;
}

int eventfd_read(int descriptor, eventfd_t *value) {
    return read(descriptor, value, sizeof(*value)) == (ssize_t)sizeof(*value)
               ? 0 : -1;
}

int eventfd_write(int descriptor, eventfd_t value) {
    return write(descriptor, &value, sizeof(value)) == (ssize_t)sizeof(value)
               ? 0 : -1;
}
