#ifndef AKAOS_LIBC_SYS_UIO_H
#define AKAOS_LIBC_SYS_UIO_H

#include <stddef.h>
#include <sys/types.h>

#define UIO_MAXIOV 16
#ifndef IOV_MAX
#define IOV_MAX UIO_MAXIOV
#endif

struct iovec {
    void *iov_base;
    size_t iov_len;
};

#ifdef __cplusplus
extern "C" {
#endif

ssize_t readv(int descriptor, const struct iovec *vectors, int vector_count);
ssize_t writev(int descriptor, const struct iovec *vectors, int vector_count);

#ifdef __cplusplus
}
#endif

#endif
