#ifndef AKAOS_SYSCALL_H
#define AKAOS_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* Provisional native akaOS syscall ABI. */
#define AKAOS_SYSCALL_READ 0
#define AKAOS_SYSCALL_WRITE 1
#define AKAOS_SYSCALL_EXIT 2
#define AKAOS_SYSCALL_CLOSE 3
#define AKAOS_SYSCALL_DUP 4
#define AKAOS_SYSCALL_DUP2 5
#define AKAOS_SYSCALL_OPEN 6
#define AKAOS_SYSCALL_STAT 7
#define AKAOS_SYSCALL_GETDENTS 8
#define AKAOS_SYSCALL_GETCWD 9
#define AKAOS_SYSCALL_CHDIR 10
#define AKAOS_SYSCALL_MKDIR 11
#define AKAOS_SYSCALL_UNLINK 12
#define AKAOS_SYSCALL_SPAWN 13
#define AKAOS_SYSCALL_WAITPID 14
#define AKAOS_SYSCALL_BRK 15
#define AKAOS_SYSCALL_PIPE 16
#define AKAOS_SYSCALL_KILL 17
#define AKAOS_SYSCALL_SYNC 18
#define AKAOS_SYSCALL_REBOOT 19
#define AKAOS_SYSCALL_CHMOD 20
#define AKAOS_SYSCALL_CHOWN 21
#define AKAOS_SYSCALL_GETUID 22
#define AKAOS_SYSCALL_GETEUID 23
#define AKAOS_SYSCALL_GETGID 24
#define AKAOS_SYSCALL_GETEGID 25
#define AKAOS_SYSCALL_SETUID 26
#define AKAOS_SYSCALL_SETGID 27
#define AKAOS_SYSCALL_TTY_ECHO 28
#define AKAOS_SYSCALL_SIGNAL_MODE 29
#define AKAOS_SYSCALL_SIGACTION 30
#define AKAOS_SYSCALL_SETPGID 31
#define AKAOS_SYSCALL_GETPGID 32
#define AKAOS_SYSCALL_TCSETPGRP 33
#define AKAOS_SYSCALL_TCGETPGRP 34
#define AKAOS_SYSCALL_FORK 35
#define AKAOS_SYSCALL_SIGRETURN 36
#define AKAOS_SYSCALL_GETPID 37
#define AKAOS_SYSCALL_EXECVE 38
#define AKAOS_SYSCALL_MMAP 39
#define AKAOS_SYSCALL_MUNMAP 40
#define AKAOS_SYSCALL_MPROTECT 41
#define AKAOS_SYSCALL_SHM_OPEN 42
#define AKAOS_SYSCALL_SHM_UNLINK 43
#define AKAOS_SYSCALL_FTRUNCATE 44
#define AKAOS_SYSCALL_POLL 45
#define AKAOS_SYSCALL_GETSID 46
#define AKAOS_SYSCALL_SETSID 47
#define AKAOS_SYSCALL_PTSNAME 48
#define AKAOS_SYSCALL_IOCTL 49
#define AKAOS_SYSCALL_SOCKET 50
#define AKAOS_SYSCALL_CONNECT 51
#define AKAOS_SYSCALL_BIND 52
#define AKAOS_SYSCALL_LISTEN 53
#define AKAOS_SYSCALL_ACCEPT 54
#define AKAOS_SYSCALL_SENDTO 55
#define AKAOS_SYSCALL_RECVFROM 56
#define AKAOS_SYSCALL_MOUNT 57
#define AKAOS_SYSCALL_UMOUNT 58
#define AKAOS_SYSCALL_RENAME 59
#define AKAOS_SYSCALL_LSEEK 60
/* Voluntarily give the current thread's time slice to another runnable task. */
#define AKAOS_SYSCALL_YIELD 61
/* Native user-thread ABI.  THREAD_CREATE shares the caller's address space;
 * its arguments are (entry, user_stack, opaque_arg). */
#define AKAOS_SYSCALL_THREAD_CREATE 62
#define AKAOS_SYSCALL_THREAD_EXIT 63
#define AKAOS_SYSCALL_THREAD_JOIN 64
#define AKAOS_SYSCALL_THREAD_DETACH 65
#define AKAOS_SYSCALL_GETTID 66
/* Return the native thread state for a TID (0 for an unknown/reaped TID). */
#define AKAOS_SYSCALL_THREAD_STATE 67
/* Spawn a program with an explicit argv/envp environment in one native
 * process-creation operation.  This avoids the fork/exec address-space
 * transition and keeps posix_spawn portable on every supported CPU. */
#define AKAOS_SYSCALL_SPAWNVE 68
/* Native user thread-pointer ABI used by libc static TLS and pthreads. */
#define AKAOS_SYSCALL_SET_TP 69
#define AKAOS_SYSCALL_GET_TP 70
#define AKAOS_SYSCALL_FUTEX 71
#define AKAOS_SYSCALL_GETSOCKOPT 72
/* Return the current process's recorded parent PID. */
#define AKAOS_SYSCALL_GETPPID 73
#define AKAOS_SYSCALL_SENDMSG 74
#define AKAOS_SYSCALL_RECVMSG 75
#define AKAOS_SYSCALL_FSTAT 76
#define AKAOS_SYSCALL_OPENAT 77
#define AKAOS_SYSCALL_UNLINKAT 78
#define AKAOS_SYSCALL_FCNTL 79

/* Supported fcntl commands and descriptor flags.  Keep these in the native
 * syscall ABI so libc and the kernel share one definition on every CPU. */
#define AKAOS_F_DUPFD 0
#define AKAOS_F_GETFD 1
#define AKAOS_F_SETFD 2
#define AKAOS_F_GETFL 3
#define AKAOS_F_SETFL 4
#define AKAOS_F_ADD_SEALS 1033
#define AKAOS_F_GET_SEALS 1034
#define AKAOS_F_DUPFD_CLOEXEC 1030
#define AKAOS_FD_CLOEXEC 1
#define AKAOS_F_SEAL_SEAL UINT32_C(0x0001)
#define AKAOS_F_SEAL_SHRINK UINT32_C(0x0002)
#define AKAOS_F_SEAL_GROW UINT32_C(0x0004)
#define AKAOS_F_SEAL_WRITE UINT32_C(0x0008)
#define AKAOS_F_SEAL_FUTURE_WRITE UINT32_C(0x0010)
#define AKAOS_MFD_CLOEXEC UINT32_C(0x0001)
#define AKAOS_MFD_ALLOW_SEALING UINT32_C(0x0002)
#define AKAOS_MEMFD_NAME_MAX 249U
#define AKAOS_SYSCALL_SIGPROCMASK 80
#define AKAOS_SYSCALL_GETSOCKNAME 81
#define AKAOS_SYSCALL_GETPEERNAME 82
#define AKAOS_SYSCALL_SETSOCKOPT 83
#define AKAOS_SYSCALL_SHUTDOWN 84
#define AKAOS_SYSCALL_SIGNALFD 85
#define AKAOS_SYSCALL_MEMFD_CREATE 86
#define AKAOS_SYSCALL_FLOCK 87
#define AKAOS_SYSCALL_MKDIRAT 88
#define AKAOS_SYSCALL_RENAMEAT 89
#define AKAOS_SYSCALL_FCHDIR 90
#define AKAOS_SYSCALL_FCHMOD 91
#define AKAOS_SYSCALL_FCHOWN 92

#define AKAOS_LOCK_SH 1
#define AKAOS_LOCK_EX 2
#define AKAOS_LOCK_NB 4
#define AKAOS_LOCK_UN 8

#define AKAOS_SFD_CLOEXEC UINT32_C(0x80000)
#define AKAOS_SFD_NONBLOCK UINT32_C(0x800)

/* Stable payload for openat(2), using a 64-bit user pointer on all targets. */
struct akaos_openat_args {
    int32_t directory;
    uint32_t flags;
    uint32_t mode;
    uint32_t reserved;
    uint64_t path;
};

struct akaos_unlinkat_args {
    int32_t directory;
    uint32_t flags;
    uint64_t path;
};

struct akaos_mkdirat_args {
    int32_t directory;
    uint32_t mode;
    uint64_t path;
};

struct akaos_renameat_args {
    int32_t old_directory;
    int32_t new_directory;
    uint64_t old_path;
    uint64_t new_path;
};

struct akaos_memfd_create_args {
    uint64_t name;
    uint32_t name_length;
    uint32_t flags;
};

#define AKAOS_AT_FDCWD (-100)
#define AKAOS_AT_REMOVEDIR UINT32_C(0x200)
#define AKAOS_AT_SYMLINK_FOLLOW UINT32_C(0x400)
#define AKAOS_OPEN_DIRECTORY UINT32_C(0x10000)
#define AKAOS_OPEN_NOFOLLOW UINT32_C(0x20000)
#define AKAOS_FUTEX_WAIT 0U
#define AKAOS_FUTEX_WAKE 1U
#define AKAOS_FUTEX_WAIT_TIMEOUT 2U
#define AKAOS_FUTEX_PRIVATE_FLAG 128U

/* Payload for AKAOS_FUTEX_WAIT_TIMEOUT. `seconds` and `nanoseconds` describe
 * a relative timeout; `expected` is compared atomically with the futex word
 * before the caller is queued. */
struct akaos_futex_timeout {
    uint32_t expected;
    uint32_t reserved;
    uint64_t seconds;
    uint32_t nanoseconds;
    uint32_t reserved2;
};

/* Linux-compatible SO_PEERCRED payload. Keep pid signed and all members
 * 32-bit so the structure is identical on every supported architecture. */
struct akaos_ucred {
    int32_t pid;
    uint32_t uid;
    uint32_t gid;
};

/* Stable payload passed through AKAOS_SYSCALL_GETSOCKOPT. Pointer fields are
 * 64-bit even when a 32-bit userspace program is calling the kernel. */
struct akaos_getsockopt_args {
    uint64_t sockfd;
    uint32_t level;
    uint32_t option;
    uint64_t optval;
    uint64_t optlen;
};

struct akaos_setsockopt_args {
    uint64_t sockfd;
    uint32_t level;
    uint32_t option;
    uint64_t optval;
    uint64_t optlen;
};

#define AKAOS_SOL_SOCKET 1U
#define AKAOS_SO_REUSEADDR 2U
#define AKAOS_SO_TYPE 3U
#define AKAOS_SO_ERROR 4U
#define AKAOS_SO_BROADCAST 6U
#define AKAOS_SO_SNDBUF 7U
#define AKAOS_SO_RCVBUF 8U
#define AKAOS_SO_KEEPALIVE 9U
#define AKAOS_SO_REUSEPORT 15U
#define AKAOS_SO_PEERCRED 17U
#define AKAOS_SO_DOMAIN 39U
#define AKAOS_IPPROTO_IP 0U
#define AKAOS_IPPROTO_TCP 6U
#define AKAOS_IPPROTO_UDP 17U
#define AKAOS_IPPROTO_IPV6 41U
#define AKAOS_IP_TOS 1U
#define AKAOS_IP_TTL 2U
#define AKAOS_IP_MULTICAST_TTL 33U
#define AKAOS_IP_MULTICAST_LOOP 34U
#define AKAOS_IPV6_UNICAST_HOPS 16U
#define AKAOS_IPV6_MULTICAST_HOPS 18U
#define AKAOS_IPV6_MULTICAST_LOOP 19U
#define AKAOS_IPV6_V6ONLY 26U
#define AKAOS_TCP_NODELAY 1U
#define AKAOS_SCM_RIGHTS 1U
#define AKAOS_MSG_CTRUNC 0x08U
#define AKAOS_MSG_TRUNC 0x20U
#define AKAOS_MSG_DONTWAIT 0x40U
#define AKAOS_MSG_CMSG_CLOEXEC 0x40000000U
#define AKAOS_MSG_NOSIGNAL 0x4000U
#define AKAOS_SOCKET_MSG_MAX_IOV 16U
#define AKAOS_SOCKET_MSG_MAX_DATA 8192U
#define AKAOS_SOCKET_MSG_MAX_CONTROL 256U
#define AKAOS_SOCKET_RIGHTS_MAX 4U
#define AKAOS_SOCKET_RIGHTS_QUEUE 8U

/* Native-width layouts shared by libc and the kernel. They intentionally
 * match the target ABI's POSIX msghdr/iovec/cmsghdr field order while keeping
 * pointer fields correct for both 32-bit and 64-bit builds. */
struct akaos_socket_iovec {
    uintptr_t base;
    size_t length;
};

struct akaos_socket_msghdr {
    uintptr_t name;
    uint32_t name_length;
    struct akaos_socket_iovec *iov;
    size_t iov_count;
    uintptr_t control;
    size_t control_length;
    int32_t flags;
};

struct akaos_socket_cmsghdr {
    size_t length;
    int32_t level;
    int32_t type;
};
#define AKAOS_THREAD_STATE_UNUSED 0
#define AKAOS_THREAD_STATE_READY 1
#define AKAOS_THREAD_STATE_RUNNING 2
#define AKAOS_THREAD_STATE_BLOCKED 3
#define AKAOS_THREAD_STATE_EXITED 4
#define AKAOS_THREAD_STATE_ALLOCATING 5
#define AKAOS_THREAD_STATE_REAPING 6

#define AKAOS_SEEK_SET 0
#define AKAOS_SEEK_CUR 1
#define AKAOS_SEEK_END 2

#define AKAOS_RB_REBOOT 1
#define AKAOS_RB_POWEROFF 2
#define AKAOS_RB_HALT 3

#define AKAOS_SYSCALL_MAX_IO 32768
#define AKAOS_SYSCALL_PATH_MAX 127

/* User pointers are represented as 64-bit values in the native syscall
 * structures so the same ABI can be decoded by 32- and 64-bit kernels. */
struct akaos_spawnve_args {
    uint64_t path;
    uint64_t argv;
    uint64_t envp;
    uint64_t out_pid;
    uint64_t actions;
    uint32_t action_count;
    uint32_t reserved;
    uint32_t spawn_flags;
    uint32_t process_group;
    uint32_t signal_mask;
    uint32_t signal_default;
};

#define AKAOS_POSIX_SPAWN_RESETIDS UINT32_C(0x01)
#define AKAOS_POSIX_SPAWN_SETPGROUP UINT32_C(0x02)
#define AKAOS_POSIX_SPAWN_SETSIGDEF UINT32_C(0x04)
#define AKAOS_POSIX_SPAWN_SETSIGMASK UINT32_C(0x08)

#define AKAOS_SPAWN_ACTION_CLOSE 1U
#define AKAOS_SPAWN_ACTION_DUP2 2U
#define AKAOS_SPAWN_ACTION_CLOSEFROM 3U

struct akaos_spawn_file_action {
    uint32_t type;
    int32_t first;
    int32_t second;
    uint32_t flags;
    uint64_t path;
    uint64_t mode;
};

#endif
