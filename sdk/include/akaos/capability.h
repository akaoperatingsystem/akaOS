#ifndef AKAOS_CAPABILITY_H
#define AKAOS_CAPABILITY_H

#include <stdint.h>
#include <stddef.h>

/* ================================================================
 * akaOS POSIX Capabilities Architecture
 * ================================================================ */

typedef uint32_t akaos_cap_t;

/* Standard Capabilities */
#define AKAOS_CAP_CHOWN             (UINT32_C(1) << 0)  /* Make arbitrary changes to file UIDs/GIDs */
#define AKAOS_CAP_DAC_OVERRIDE      (UINT32_C(1) << 1)  /* Bypass file read, write, and execute checks */
#define AKAOS_CAP_DAC_READ_SEARCH   (UINT32_C(1) << 2)  /* Bypass file read and directory search checks */
#define AKAOS_CAP_FOWNER            (UINT32_C(1) << 3)  /* Bypass permission checks for owner-only operations */
#define AKAOS_CAP_FSETID            (UINT32_C(1) << 4)  /* Don't clear set-user-ID and set-group-ID bits */
#define AKAOS_CAP_KILL              (UINT32_C(1) << 5)  /* Bypass permission checks for sending signals */
#define AKAOS_CAP_SETGID            (UINT32_C(1) << 6)  /* Arbitrary manipulation of process GIDs */
#define AKAOS_CAP_SETUID            (UINT32_C(1) << 7)  /* Arbitrary manipulation of process UIDs */
#define AKAOS_CAP_NET_BIND_SERVICE  (UINT32_C(1) << 8)  /* Bind a socket to privilege ports (< 1024) */
#define AKAOS_CAP_NET_ADMIN         (UINT32_C(1) << 9)  /* Interface and routing configuration */
#define AKAOS_CAP_NET_RAW           (UINT32_C(1) << 10) /* Use RAW and PACKET sockets */
#define AKAOS_CAP_SYS_ADMIN         (UINT32_C(1) << 11) /* Broad system administration operations */
#define AKAOS_CAP_SYS_RAWIO         (UINT32_C(1) << 12) /* Perform I/O port operations (iopl, inb, outb) */
#define AKAOS_CAP_SYS_BOOT          (UINT32_C(1) << 13) /* Reboot, shutdown, or kexec */
#define AKAOS_CAP_SYS_PTRACE        (UINT32_C(1) << 14) /* Trace arbitrary processes */
#define AKAOS_CAP_SYS_TIME          (UINT32_C(1) << 15) /* Set system clock and real-time clock */

/* Monolithic full root capability mask */
#define AKAOS_CAP_ROOT              UINT32_C(0xFFFFFFFF)
#define AKAOS_CAP_NONE              UINT32_C(0x00000000)

struct akaos_cap {
    akaos_cap_t effective;
    akaos_cap_t permitted;
    akaos_cap_t inheritable;
};

#endif /* AKAOS_CAPABILITY_H */
