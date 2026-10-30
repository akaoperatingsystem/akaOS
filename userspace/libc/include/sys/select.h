#ifndef AKAOS_LIBC_SYS_SELECT_H
#define AKAOS_LIBC_SYS_SELECT_H

#include <time.h>

/* The kernel poll ABI is bounded to 16 descriptors; keep the public fd_set
 * large enough for normal POSIX callers while select() reports EINVAL when a
 * request cannot be represented by that native bound. */
#define FD_SETSIZE 2048
#define FD_WORD_BITS (8U * sizeof(unsigned long))
#define FD_WORD_COUNT ((FD_SETSIZE + FD_WORD_BITS - 1U) / FD_WORD_BITS)

/* The member is named fds_bits because that is the conventional field name
 * for the descriptor bitmap.  Upstream X.Org libraries (including the
 * libX11 socket transport) access fd_set.fds_bits directly, and the X.Org
 * configure scripts refuse to build when the field cannot be found. */
typedef struct {
    unsigned long fds_bits[FD_WORD_COUNT];
} fd_set;

#define FD_ZERO(set) do { \
    fd_set *_fd_set = (set); \
    for (unsigned _fd_index = 0; _fd_index < FD_WORD_COUNT; ++_fd_index) \
        _fd_set->fds_bits[_fd_index] = 0UL; \
} while (0)

#define FD_SET(fd, set) do { \
    int _fd_value = (fd); \
    if (_fd_value >= 0 && _fd_value < FD_SETSIZE) \
        (set)->fds_bits[(unsigned)_fd_value / FD_WORD_BITS] |= \
            1UL << ((unsigned)_fd_value % FD_WORD_BITS); \
} while (0)

#define FD_CLR(fd, set) do { \
    int _fd_value = (fd); \
    if (_fd_value >= 0 && _fd_value < FD_SETSIZE) \
        (set)->fds_bits[(unsigned)_fd_value / FD_WORD_BITS] &= \
            ~(1UL << ((unsigned)_fd_value % FD_WORD_BITS)); \
} while (0)

#define FD_ISSET(fd, set) \
    ((fd) >= 0 && (fd) < FD_SETSIZE && \
     ((set)->fds_bits[(unsigned)(fd) / FD_WORD_BITS] & \
      (1UL << ((unsigned)(fd) % FD_WORD_BITS))) != 0UL)

#ifdef __cplusplus
extern "C" {
#endif
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);
#ifdef __cplusplus
}
#endif

#endif
