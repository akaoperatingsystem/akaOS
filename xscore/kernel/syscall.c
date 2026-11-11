#include <xscore/bufcache.h>
#include <xscore/file.h>
#include <xscore/heap.h>
#include <xscore/process.h>
#include <xscore/serial.h>
#include <xscore/syscall.h>
#include <xscore/thread.h>
#include <xscore/timer.h>
#include <xscore/tty.h>
#include <xscore/usercopy.h>
#include <xscore/virtio_blk.h>
#include <xscore/vmm.h>
#include <xscore/vfs.h>
#include <akaos/errno.h>
#include <akaos/fs.h>
#include <akaos/mman.h>
#include <akaos/poll.h>
#include <akaos/termios.h>
#include <akaos/input.h>
#include <akaos/display.h>
#include <akaos/ioctl.h>
#include <akaos/syscall.h>
#include <akaos/signal.h>
#include <akaos/net.h>
#include <xscore/socket.h>
#include <xscore/acpi.h>
#include <xscore/smp.h>
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);

static int authorize_device_open(uint32_t cwd_node, const char *path,
                                 uint64_t flags) {
    uint64_t access = flags & UINT64_C(3);
    if (access > AKAOS_O_RDWR) return -AKAOS_EINVAL;
    uint32_t requested = 0U;
    if (access != AKAOS_O_WRONLY) requested |= 4U;
    if (access != AKAOS_O_RDONLY) requested |= 2U;
    return xscore_vfs_access_at(cwd_node, path, requested);
}

static int64_t open_character_device(struct xscore_fd_table *table,
                                     enum xscore_device_kind kind,
                                     uint64_t flags) {
    uint64_t access = flags & UINT64_C(3);
    if (access > AKAOS_O_RDWR) return -AKAOS_EINVAL;
    int readable = access != AKAOS_O_WRONLY;
    int writable = access != AKAOS_O_RDONLY;
    if (kind == XSCORE_DEVICE_TTY1) {
        return xscore_fd_open_tty(table, readable, writable);
    }
    if (kind == XSCORE_DEVICE_NULL) {
        return xscore_fd_open_null(table, readable, writable);
    }
    if (kind == XSCORE_DEVICE_ZERO) {
        return xscore_fd_open_zero(table, readable, writable);
    }
    if (kind == XSCORE_DEVICE_RANDOM) {
        if (!readable || writable) return -AKAOS_EACCES;
        return xscore_fd_open_random(table);
    }
    return -AKAOS_ENOENT;
}

static int copy_at_path_from_user(
    char path[AKAOS_SYSCALL_PATH_MAX + 1U], uint64_t user_path) {
    if (user_path == 0U ||
        !xscore_copy_string_from_user(path, AKAOS_SYSCALL_PATH_MAX + 1U,
                                      user_path, 0))
        return -AKAOS_EFAULT;
    size_t length = 0U;
    while (length <= AKAOS_SYSCALL_PATH_MAX && path[length] != '\0')
        ++length;
    if (length == 0U) return -AKAOS_EINVAL;
    if (length > AKAOS_SYSCALL_PATH_MAX) return -AKAOS_ENAMETOOLONG;
    return 0;
}

static int resolve_at_directory(struct xscore_fd_table *table,
                                uint32_t current_cwd, int directory,
                                const char *path, uint32_t *resolved_cwd) {
    if (!table || !path || !resolved_cwd) return -AKAOS_EBADF;
    *resolved_cwd = current_cwd;
    if (directory == AKAOS_AT_FDCWD || path[0] == '/') return 0;
    if (xscore_fd_directory_state(table, directory, resolved_cwd, 0))
        return 0;
    uint32_t ignored_node = 0U;
    if (!xscore_fd_vnode_state(table, directory, &ignored_node, 0, 0))
        return -AKAOS_EBADF;
    return -AKAOS_ENOTDIR;
}

static int64_t copy_sockaddr_un_from_user(
    struct akaos_sockaddr_un *address, uint64_t user_address,
    uint64_t address_length) {
    if (!address) return -AKAOS_EINVAL;
    size_t path_offset = offsetof(struct akaos_sockaddr_un, sun_path);
    if (address_length < path_offset + 1U ||
        address_length > sizeof(*address)) return -AKAOS_EINVAL;
    memset(address, 0, sizeof(*address));
    if (!xscore_copy_from_user(address, user_address,
                               (size_t)address_length)) return -AKAOS_EFAULT;
    size_t path_length = (size_t)address_length - path_offset;
    if (path_length < sizeof(address->sun_path)) {
        address->sun_path[path_length] = '\0';
    } else if (address->sun_path[path_length - 1U] != '\0') {
        return -AKAOS_ENAMETOOLONG;
    }
    if (address->sun_path[0] == '\0') return -AKAOS_EINVAL;
    return 0;
}

static uint64_t poll_clock_now(void) {
#if defined(__x86_64__) || defined(__i386__)
    return xscore_timer_ticks();
#elif defined(__aarch64__)
    uint64_t value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
#elif defined(__arm__)
    uint32_t low, high;
    __asm__ volatile("mrrc p15, 1, %0, %1, c14"
                     : "=r"(low), "=r"(high));
    return ((uint64_t)high << 32) | low;
#elif defined(__riscv) && __riscv_xlen == 64
    uint64_t value;
    __asm__ volatile("rdtime %0" : "=r"(value));
    return value;
#elif defined(__riscv)
    uint32_t high_before, low, high_after;
    do {
        __asm__ volatile("rdtimeh %0" : "=r"(high_before));
        __asm__ volatile("rdtime %0" : "=r"(low));
        __asm__ volatile("rdtimeh %0" : "=r"(high_after));
    } while (high_before != high_after);
    return ((uint64_t)high_before << 32) | low;
#else
    return 0;
#endif
}

static uint64_t poll_clock_delta(int timeout_ms) {
#if defined(__x86_64__) || defined(__i386__)
    /* The current x86 timer is 100 Hz: round positive millisecond waits up
     * to the next hardware tick. */
    return (uint64_t)(((uint32_t)timeout_ms + UINT32_C(9)) /
                      UINT32_C(10));
#elif defined(__aarch64__)
    uint64_t frequency;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    return (frequency / 1000) * (uint64_t)(uint32_t)timeout_ms;
#elif defined(__arm__)
    uint32_t frequency;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(frequency));
    return (uint64_t)(frequency / 1000) * (uint32_t)timeout_ms;
#elif defined(__riscv)
    /* QEMU virt and OpenSBI expose the architectural time counter at 10 MHz. */
    return UINT64_C(10000) * (uint64_t)(uint32_t)timeout_ms;
#else
    return (uint64_t)(uint32_t)timeout_ms;
#endif
}

#if defined(__aarch64__)
static void arch_psci_system_call(uint64_t function_id) {
    register uint64_t x0 __asm__("x0") = function_id;
    __asm__ volatile("hvc #0" : "+r"(x0) : : "x1", "x2", "x3", "memory");
}
#elif defined(__arm__)
static void arch_psci_system_call(uint32_t function_id) {
    register uint32_t r0 __asm__("r0") = function_id;
    __asm__ volatile("hvc #0" : "+r"(r0) : : "r1", "r2", "r3", "memory");
}
#elif defined(__riscv)
static void arch_sbi_system_reset(uintptr_t reset_type) {
    register uintptr_t a0 __asm__("a0") = reset_type;
    register uintptr_t a1 __asm__("a1") = 0;
    register uintptr_t a6 __asm__("a6") = 0;
    register uintptr_t a7 __asm__("a7") = (uintptr_t)0x53525354;
    __asm__ volatile("ecall"
                     : "+r"(a0), "+r"(a1)
                     : "r"(a6), "r"(a7)
                     : "memory");
}
#endif

void xscore_syscall_prepare_process(void) {
    xscore_thread_prepare_user_transition();
}

#define XSCORE_FUTEX_WAITERS XSCORE_MAX_THREADS

struct xscore_futex_waiter {
    uint64_t address_space_root;
    uint64_t address;
    uint64_t thread_id;
    uint64_t deadline_ticks;
    uint32_t result;
    uint32_t timed;
    uint32_t used;
};

#define XSCORE_FUTEX_RESULT_PENDING 0U
#define XSCORE_FUTEX_RESULT_WOKEN 1U
#define XSCORE_FUTEX_RESULT_TIMED_OUT 2U

static struct xscore_futex_waiter futex_waiters[XSCORE_FUTEX_WAITERS];
static xscore_spinlock_t futex_wait_lock = XSCORE_SPINLOCK_INIT;

static uint64_t futex_divide_u64_u32(uint64_t dividend, uint32_t divisor,
                                     uint32_t *remainder_out) {
    uint64_t quotient = 0U;
    uint64_t remainder = 0U;
    if (divisor == 0U) {
        if (remainder_out) *remainder_out = 0U;
        return UINT64_MAX;
    }
    for (int bit = 63; bit >= 0; --bit) {
        remainder = (remainder << 1) |
            ((dividend >> (uint32_t)bit) & UINT64_C(1));
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= UINT64_C(1) << (uint32_t)bit;
        }
    }
    if (remainder_out) *remainder_out = (uint32_t)remainder;
    return quotient;
}

static uint64_t futex_saturating_multiply_u64_u32(uint64_t value,
                                                   uint32_t factor) {
    if (value == 0U || factor == 0U) return 0U;
    uint32_t high = (uint32_t)(value >> 32);
    uint32_t low = (uint32_t)value;
    if (high > UINT32_MAX / factor) return UINT64_MAX;
    uint64_t high_product = (uint64_t)high * factor;
    uint64_t shifted_high = high_product << 32;
    uint64_t low_product = (uint64_t)low * factor;
    if (UINT64_MAX - shifted_high < low_product) return UINT64_MAX;
    return shifted_high + low_product;
}

static uint64_t futex_timeout_ticks(const struct akaos_futex_timeout *timeout) {
    uint32_t frequency = xscore_timer_frequency();
    if (frequency == 0U) frequency = 100U;
    uint64_t whole_ticks =
        futex_saturating_multiply_u64_u32(timeout->seconds, frequency);
    if (whole_ticks == UINT64_MAX) return UINT64_MAX;

    uint64_t fractional_product =
        (uint64_t)timeout->nanoseconds * (uint64_t)frequency;
    uint32_t remainder = 0U;
    uint64_t fractional_ticks = futex_divide_u64_u32(
        fractional_product, UINT32_C(1000000000), &remainder);
    if (remainder != 0U) ++fractional_ticks;
    if (UINT64_MAX - whole_ticks < fractional_ticks) return UINT64_MAX;
    return whole_ticks + fractional_ticks;
}

static int64_t syscall_futex(struct xscore_process *process, uint64_t address,
                             uint64_t operation, uint64_t value) {
    if (!process || address == 0U ||
        (address & (sizeof(uint32_t) - 1U)) != 0U)
        return -AKAOS_EINVAL;
    uint64_t command = operation & ~((uint64_t)AKAOS_FUTEX_PRIVATE_FLAG);
    if ((operation & ~((uint64_t)AKAOS_FUTEX_PRIVATE_FLAG | UINT64_C(0xff))) != 0U ||
        (command != AKAOS_FUTEX_WAIT && command != AKAOS_FUTEX_WAKE &&
         command != AKAOS_FUTEX_WAIT_TIMEOUT))
        return -AKAOS_ENOSYS;
    if (!xscore_user_range_prepare(address, sizeof(uint32_t), 0))
        return -AKAOS_EFAULT;

    int timed = command == AKAOS_FUTEX_WAIT_TIMEOUT;
    struct akaos_futex_timeout timeout = {0};
    uint32_t expected = (uint32_t)value;
    uint64_t deadline_ticks = 0U;
    if (timed) {
        if (value == 0U ||
            !xscore_user_range_prepare(value, sizeof(timeout), 0) ||
            !xscore_copy_from_user(&timeout, value, sizeof(timeout)))
            return -AKAOS_EFAULT;
        if (timeout.reserved != 0U || timeout.reserved2 != 0U ||
            timeout.nanoseconds >= UINT32_C(1000000000))
            return -AKAOS_EINVAL;
        expected = timeout.expected;
        uint64_t now = xscore_timer_ticks();
        uint64_t delta = futex_timeout_ticks(&timeout);
        deadline_ticks = UINT64_MAX - now < delta
            ? UINT64_MAX : now + delta;
    }

    uint64_t root = xscore_process_address_space_root(process);
    uint64_t page_flags = 0U;
    uint64_t physical_address = 0U;
    if (!root || !xscore_process_page_info(process, address, &page_flags,
                                           &physical_address) ||
        (page_flags & XSCORE_PAGE_USER) == 0U || physical_address == 0U)
        return -AKAOS_EFAULT;
    uint64_t key_root =
        (operation & AKAOS_FUTEX_PRIVATE_FLAG) != 0U ? root : 0U;
    uint64_t key_address = key_root != 0U
        ? address
        : physical_address + (address & UINT64_C(0xfff));
    volatile uint32_t *word = (volatile uint32_t *)(uintptr_t)address;
    if (command == AKAOS_FUTEX_WAIT || timed) {
        uint64_t thread_id = xscore_thread_current_id();
        if (!thread_id) return -AKAOS_EINVAL;
        uintptr_t flags = xscore_spin_lock_irqsave(&futex_wait_lock);
        if (__atomic_load_n(word, __ATOMIC_ACQUIRE) != expected) {
            xscore_spin_unlock_irqrestore(&futex_wait_lock, flags);
            return -AKAOS_EAGAIN;
        }
        if (timed && xscore_timer_ticks() >= deadline_ticks) {
            xscore_spin_unlock_irqrestore(&futex_wait_lock, flags);
            return -AKAOS_ETIMEDOUT;
        }
        uint32_t slot = XSCORE_FUTEX_WAITERS;
        for (uint32_t index = 0; index < XSCORE_FUTEX_WAITERS; ++index) {
            if (futex_waiters[index].used &&
                xscore_thread_state(futex_waiters[index].thread_id) ==
                    XSCORE_THREAD_UNUSED)
                futex_waiters[index].used = 0U;
            if (futex_waiters[index].used &&
                futex_waiters[index].thread_id == thread_id) {
                xscore_spin_unlock_irqrestore(&futex_wait_lock, flags);
                return -AKAOS_EBUSY;
            }
            if (!futex_waiters[index].used && slot == XSCORE_FUTEX_WAITERS)
                slot = index;
        }
        if (slot == XSCORE_FUTEX_WAITERS) {
            xscore_spin_unlock_irqrestore(&futex_wait_lock, flags);
            return -AKAOS_EAGAIN;
        }
        futex_waiters[slot].address_space_root = key_root;
        futex_waiters[slot].address = key_address;
        futex_waiters[slot].thread_id = thread_id;
        futex_waiters[slot].deadline_ticks = deadline_ticks;
        futex_waiters[slot].result = XSCORE_FUTEX_RESULT_PENDING;
        futex_waiters[slot].timed = timed != 0;
        futex_waiters[slot].used = 1U;
        xscore_spin_unlock_irqrestore(&futex_wait_lock, flags);

        (void)xscore_thread_block();
        flags = xscore_spin_lock_irqsave(&futex_wait_lock);
        uint32_t wait_result = XSCORE_FUTEX_RESULT_PENDING;
        for (uint32_t index = 0; index < XSCORE_FUTEX_WAITERS; ++index) {
            struct xscore_futex_waiter *waiter = &futex_waiters[index];
            if (waiter->used && waiter->thread_id == thread_id) {
                if (waiter->address_space_root == key_root &&
                    waiter->address == key_address)
                    wait_result = waiter->result;
                waiter->used = 0U;
                break;
            }
        }
        xscore_spin_unlock_irqrestore(&futex_wait_lock, flags);
        if (wait_result == XSCORE_FUTEX_RESULT_WOKEN) return 0;
        if (wait_result == XSCORE_FUTEX_RESULT_TIMED_OUT)
            return -AKAOS_ETIMEDOUT;
        return -AKAOS_EAGAIN;
    }

    if (value == 0U) return 0;
    uint32_t maximum = value > XSCORE_FUTEX_WAITERS
        ? XSCORE_FUTEX_WAITERS : (uint32_t)value;
    uint32_t woken = 0U;
    uintptr_t flags = xscore_spin_lock_irqsave(&futex_wait_lock);
    for (uint32_t index = 0; index < XSCORE_FUTEX_WAITERS && woken < maximum;
         ++index) {
        struct xscore_futex_waiter *waiter = &futex_waiters[index];
        if (!waiter->used || waiter->result != XSCORE_FUTEX_RESULT_PENDING ||
            waiter->address_space_root != key_root ||
            waiter->address != key_address)
            continue;
        uint64_t thread_id = waiter->thread_id;
        if (xscore_thread_wake(thread_id)) {
            waiter->result = XSCORE_FUTEX_RESULT_WOKEN;
            ++woken;
        } else {
            waiter->used = 0U;
        }
    }
    xscore_spin_unlock_irqrestore(&futex_wait_lock, flags);
    return (int64_t)woken;
}

void xscore_syscall_futex_timer_tick(void) {
    uint64_t now = xscore_timer_ticks();
    uintptr_t flags = xscore_spin_lock_irqsave(&futex_wait_lock);
    for (uint32_t index = 0; index < XSCORE_FUTEX_WAITERS; ++index) {
        struct xscore_futex_waiter *waiter = &futex_waiters[index];
        if (!waiter->used || !waiter->timed ||
            waiter->result != XSCORE_FUTEX_RESULT_PENDING ||
            now < waiter->deadline_ticks)
            continue;
        if (xscore_thread_wake(waiter->thread_id)) {
            waiter->result = XSCORE_FUTEX_RESULT_TIMED_OUT;
        } else if (xscore_thread_state(waiter->thread_id) ==
                   XSCORE_THREAD_UNUSED) {
            waiter->used = 0U;
        }
    }
    xscore_spin_unlock_irqrestore(&futex_wait_lock, flags);
}

static uint64_t check_and_deliver_pending_signals(struct xscore_process *process) {
    if (!process) return 0;
    if (xscore_process_signal_pending_current()) {
        uint32_t sig = xscore_process_pending_signal(process);
        if (xscore_process_dispatch_user_signal(0)) {
            return (uint64_t)sig;
        }
        uint32_t signal = 0;
        if (xscore_process_take_pending_signal(&signal)) {
            xscore_process_record_termination_signal(signal);
            xscore_thread_record_user_exit(UINT64_C(128) + signal);
            return XSCORE_SYSCALL_EXIT_TO_KERNEL;
        }
    }
    return 0;
}

static size_t socket_cmsg_align(size_t length) {
    const size_t alignment = sizeof(size_t);
    if (length > SIZE_MAX - (alignment - 1U)) return SIZE_MAX;
    return (length + alignment - 1U) & ~(alignment - 1U);
}

static size_t socket_cmsg_len(size_t payload_length) {
    size_t header_length = socket_cmsg_align(
        sizeof(struct akaos_socket_cmsghdr));
    if (header_length == SIZE_MAX ||
        payload_length > SIZE_MAX - header_length) return SIZE_MAX;
    return header_length + payload_length;
}

static size_t socket_cmsg_space(size_t payload_length) {
    size_t header_length = socket_cmsg_align(
        sizeof(struct akaos_socket_cmsghdr));
    size_t aligned_payload = socket_cmsg_align(payload_length);
    if (header_length == SIZE_MAX || aligned_payload == SIZE_MAX ||
        aligned_payload > SIZE_MAX - header_length) return SIZE_MAX;
    return header_length + aligned_payload;
}

static int64_t syscall_socket_sendmsg(struct xscore_fd_table *table,
                                      uint64_t descriptor,
                                      uint64_t message_address,
                                      uint64_t send_flags) {
    const uint64_t supported_flags = AKAOS_MSG_DONTWAIT |
                                     AKAOS_MSG_NOSIGNAL;
    if (!table) return -AKAOS_EBADF;
    if (descriptor > INT32_MAX) return -AKAOS_EBADF;
    if ((send_flags & ~supported_flags) != 0U) return -AKAOS_EOPNOTSUPP;

    struct akaos_socket_msghdr message;
    if (!message_address ||
        !xscore_user_range_prepare(message_address, sizeof(message), 0) ||
        !xscore_copy_from_user(&message, message_address, sizeof(message)))
        return -AKAOS_EFAULT;
    if (message.name != 0U || message.name_length != 0U)
        return -AKAOS_EOPNOTSUPP;
    if (message.iov_count > AKAOS_SOCKET_MSG_MAX_IOV ||
        message.control_length > AKAOS_SOCKET_MSG_MAX_CONTROL) {
        return -AKAOS_EINVAL;
    }
    if (message.iov_count != 0U && message.iov == 0)
        return -AKAOS_EFAULT;
    if (message.control_length != 0U && message.control == 0U)
        return -AKAOS_EFAULT;

    struct akaos_socket_iovec vectors[AKAOS_SOCKET_MSG_MAX_IOV];
    if (message.iov_count != 0U) {
        size_t vector_bytes = message.iov_count * sizeof(vectors[0]);
        uint64_t vector_address = (uint64_t)(uintptr_t)message.iov;
        if (!xscore_user_range_prepare(vector_address, vector_bytes, 0) ||
            !xscore_copy_from_user(vectors, vector_address, vector_bytes))
            return -AKAOS_EFAULT;
    }

    size_t total_length = 0U;
    for (size_t index = 0U; index < message.iov_count; ++index) {
        if (vectors[index].length >
            AKAOS_SOCKET_MSG_MAX_DATA - total_length)
            return -AKAOS_EMSGSIZE;
        if (vectors[index].length != 0U &&
            !xscore_user_range_prepare(vectors[index].base,
                                       vectors[index].length, 0))
            return -AKAOS_EFAULT;
        total_length += vectors[index].length;
    }

    uint8_t *data = xscore_heap_allocate(total_length ? total_length : 1U);
    if (!data) return -AKAOS_ENOMEM;
    size_t copied = 0U;
    for (size_t index = 0U; index < message.iov_count; ++index) {
        size_t length = vectors[index].length;
        if (length != 0U && !xscore_copy_from_user(
                data + copied, vectors[index].base, length)) {
            (void)xscore_heap_free(data);
            return -AKAOS_EFAULT;
        }
        copied += length;
    }

    int user_descriptors[AKAOS_SOCKET_RIGHTS_MAX];
    struct xscore_file *transfers[AKAOS_SOCKET_RIGHTS_MAX] = {0};
    size_t transfer_count = 0U;
    uint8_t control[AKAOS_SOCKET_MSG_MAX_CONTROL];
    if (message.control_length != 0U) {
        if (!xscore_user_range_prepare(message.control,
                                       message.control_length, 0) ||
            !xscore_copy_from_user(control, message.control,
                                   message.control_length)) {
            (void)xscore_heap_free(data);
            return -AKAOS_EFAULT;
        }
        size_t offset = 0U;
        while (offset < message.control_length) {
            size_t remaining = message.control_length - offset;
            if (remaining < sizeof(struct akaos_socket_cmsghdr)) {
                (void)xscore_heap_free(data);
                return -AKAOS_EINVAL;
            }
            struct akaos_socket_cmsghdr header;
            memcpy(&header, control + offset, sizeof(header));
            size_t minimum_length = socket_cmsg_len(0U);
            if (header.length < minimum_length ||
                header.length > remaining) {
                (void)xscore_heap_free(data);
                return -AKAOS_EINVAL;
            }
            if (header.level != (int32_t)AKAOS_SOL_SOCKET ||
                header.type != (int32_t)AKAOS_SCM_RIGHTS) {
                (void)xscore_heap_free(data);
                return -AKAOS_EOPNOTSUPP;
            }
            size_t payload_offset = socket_cmsg_align(sizeof(header));
            size_t payload_length = header.length - payload_offset;
            if (payload_length == 0U || payload_length % sizeof(int) != 0U ||
                payload_length / sizeof(int) >
                    AKAOS_SOCKET_RIGHTS_MAX - transfer_count) {
                (void)xscore_heap_free(data);
                return -AKAOS_EINVAL;
            }
            size_t descriptors_in_header = payload_length / sizeof(int);
            for (size_t item = 0U; item < descriptors_in_header; ++item) {
                memcpy(&user_descriptors[transfer_count + item],
                       control + offset + payload_offset + item * sizeof(int),
                       sizeof(int));
            }
            transfer_count += descriptors_in_header;

            size_t advance = socket_cmsg_align(header.length);
            if (advance == SIZE_MAX) {
                (void)xscore_heap_free(data);
                return -AKAOS_EINVAL;
            }
            if (advance > remaining) {
                if (header.length != remaining) {
                    (void)xscore_heap_free(data);
                    return -AKAOS_EINVAL;
                }
                offset = message.control_length;
            } else {
                offset += advance;
            }
        }
    }
    for (size_t index = 0U; index < transfer_count; ++index) {
        int64_t acquired = xscore_fd_transfer_acquire(
            table, user_descriptors[index], &transfers[index]);
        if (acquired < 0) {
            for (size_t release_index = 0U; release_index < index;
                 ++release_index)
                xscore_fd_transfer_release(transfers[release_index]);
            (void)xscore_heap_free(data);
            return acquired;
        }
    }

    int sock_id = -1;
    struct xscore_file *socket_reference = 0;
    int acquired_socket = xscore_fd_socket_acquire(
        table, (int)descriptor, &sock_id, &socket_reference);
    if (acquired_socket < 0) {
        for (size_t index = 0U; index < transfer_count; ++index)
            xscore_fd_transfer_release(transfers[index]);
        (void)xscore_heap_free(data);
        return acquired_socket;
    }
    int64_t result = xscore_socket_sendmsg(
        sock_id, data, total_length, transfers, transfer_count);
    xscore_fd_transfer_release(socket_reference);
    if (result < 0) {
        for (size_t index = 0U; index < transfer_count; ++index)
            xscore_fd_transfer_release(transfers[index]);
    }
    (void)xscore_heap_free(data);
    return result;
}

static int64_t syscall_socket_recvmsg(struct xscore_fd_table *table,
                                      uint64_t descriptor,
                                      uint64_t message_address,
                                      uint64_t receive_flags) {
    if (!table) return -AKAOS_EBADF;
    if (descriptor > INT32_MAX) return -AKAOS_EBADF;
    const uint64_t supported_flags = AKAOS_MSG_DONTWAIT |
                                     AKAOS_MSG_CMSG_CLOEXEC;
    if ((receive_flags & ~supported_flags) != 0U)
        return -AKAOS_EOPNOTSUPP;

    struct akaos_socket_msghdr message;
    if (!message_address ||
        !xscore_user_range_prepare(message_address, sizeof(message), 1) ||
        !xscore_copy_from_user(&message, message_address, sizeof(message)))
        return -AKAOS_EFAULT;
    if (message.name != 0U || message.name_length != 0U)
        return -AKAOS_EOPNOTSUPP;
    if (message.iov_count > AKAOS_SOCKET_MSG_MAX_IOV ||
        message.control_length > AKAOS_SOCKET_MSG_MAX_CONTROL) {
        return -AKAOS_EINVAL;
    }
    if (message.iov_count != 0U && message.iov == 0)
        return -AKAOS_EFAULT;
    if (message.control_length != 0U && message.control == 0U)
        return -AKAOS_EFAULT;
    if (message.control_length != 0U &&
        !xscore_user_range_prepare(message.control,
                                   message.control_length, 1))
        return -AKAOS_EFAULT;

    struct akaos_socket_iovec vectors[AKAOS_SOCKET_MSG_MAX_IOV];
    if (message.iov_count != 0U) {
        size_t vector_bytes = message.iov_count * sizeof(vectors[0]);
        uint64_t vector_address = (uint64_t)(uintptr_t)message.iov;
        if (!xscore_user_range_prepare(vector_address, vector_bytes, 0) ||
            !xscore_copy_from_user(vectors, vector_address, vector_bytes))
            return -AKAOS_EFAULT;
    }

    size_t total_capacity = 0U;
    for (size_t index = 0U; index < message.iov_count; ++index) {
        if (vectors[index].length >
            AKAOS_SOCKET_MSG_MAX_DATA - total_capacity)
            return -AKAOS_EMSGSIZE;
        if (vectors[index].length != 0U &&
            !xscore_user_range_prepare(vectors[index].base,
                                       vectors[index].length, 1))
            return -AKAOS_EFAULT;
        total_capacity += vectors[index].length;
    }

    uint8_t *data = xscore_heap_allocate(total_capacity ? total_capacity : 1U);
    if (!data) return -AKAOS_ENOMEM;
    int received_fds[AKAOS_SOCKET_RIGHTS_MAX] = {-1, -1, -1, -1};
    size_t fd_capacity = 0U;
    for (size_t count = 1U; count <= AKAOS_SOCKET_RIGHTS_MAX; ++count) {
        if (socket_cmsg_space(count * sizeof(int)) > message.control_length)
            break;
        fd_capacity = count;
    }

    int sock_id = -1;
    struct xscore_file *socket_reference = 0;
    int64_t acquired_socket = xscore_fd_socket_acquire(
        table, (int)descriptor, &sock_id, &socket_reference);
    size_t received_length = 0U;
    size_t received_count = 0U;
    int output_flags = 0;
    int64_t result = acquired_socket < 0 ? acquired_socket :
        xscore_socket_recvmsg(sock_id, table, data, total_capacity,
                              received_fds, fd_capacity, &received_length,
                              &received_count, &output_flags);
    if (socket_reference) xscore_fd_transfer_release(socket_reference);
    if (result < 0) {
        (void)xscore_heap_free(data);
        return result;
    }

    if (receive_flags & AKAOS_MSG_CMSG_CLOEXEC) {
        for (size_t index = 0U; index < received_count; ++index) {
            int64_t flag_result = xscore_fd_fcntl(
                table, received_fds[index], AKAOS_F_SETFD,
                AKAOS_FD_CLOEXEC);
            if (flag_result < 0) {
                for (size_t close_index = 0U;
                     close_index < received_count; ++close_index)
                    (void)xscore_fd_close(table, received_fds[close_index]);
                (void)xscore_heap_free(data);
                return flag_result;
            }
        }
    }

    size_t copied = 0U;
    for (size_t index = 0U; index < message.iov_count; ++index) {
        size_t remaining = received_length - copied;
        size_t length = vectors[index].length < remaining
            ? vectors[index].length : remaining;
        if (length != 0U && !xscore_copy_to_user(
                vectors[index].base, data + copied, length)) {
            result = -AKAOS_EFAULT;
            goto recvmsg_copy_failure;
        }
        copied += length;
    }

    size_t output_control_length = 0U;
    if (received_count != 0U) {
        uint8_t output_control[AKAOS_SOCKET_MSG_MAX_CONTROL];
        memset(output_control, 0, sizeof(output_control));
        struct akaos_socket_cmsghdr header;
        header.length = socket_cmsg_len(received_count * sizeof(int));
        header.level = (int32_t)AKAOS_SOL_SOCKET;
        header.type = (int32_t)AKAOS_SCM_RIGHTS;
        size_t data_offset = socket_cmsg_align(sizeof(header));
        output_control_length = socket_cmsg_space(
            received_count * sizeof(int));
        if (header.length == SIZE_MAX ||
            output_control_length > sizeof(output_control)) {
            result = -AKAOS_EINVAL;
            goto recvmsg_copy_failure;
        }
        memcpy(output_control, &header, sizeof(header));
        memcpy(output_control + data_offset, received_fds,
               received_count * sizeof(int));
        if (!xscore_copy_to_user(message.control, output_control,
                                 output_control_length)) {
            result = -AKAOS_EFAULT;
            goto recvmsg_copy_failure;
        }
    }

    message.control_length = output_control_length;
    message.flags = output_flags;
    message.name_length = 0U;
    if (!xscore_copy_to_user(message_address, &message, sizeof(message))) {
        result = -AKAOS_EFAULT;
        goto recvmsg_copy_failure;
    }
    (void)xscore_heap_free(data);
    return (int64_t)received_length;

recvmsg_copy_failure:
    for (size_t index = 0U; index < received_count; ++index)
        (void)xscore_fd_close(table, received_fds[index]);
    (void)xscore_heap_free(data);
    return result;
}



uint64_t xscore_syscall_fast_dispatch(uint64_t number, uint64_t argument0,
                                      uint64_t argument1, uint64_t argument2) {
    if (number == AKAOS_SYSCALL_GETPID) {
        struct xscore_process *p = xscore_process_current();
        return p ? (uint64_t)p->pid : 0;
    }
    if (number == AKAOS_SYSCALL_YIELD) {
        xscore_thread_yield();
        return 0;
    }
    return XSCORE_SYSCALL_NOT_HANDLED;
}

uint64_t xscore_syscall_dispatch(uint64_t number, uint64_t argument0,
                                 uint64_t argument1, uint64_t argument2) {
    struct xscore_fd_table *table = xscore_thread_current_fd_table();
    struct xscore_process *process = xscore_process_current();
    uint32_t cwd_node = process ? xscore_process_cwd_node(process) : 0;

    if (process && number != AKAOS_SYSCALL_EXIT && number != AKAOS_SYSCALL_SIGRETURN) {
        uint64_t sig_res = check_and_deliver_pending_signals(process);
        if (sig_res != 0) return sig_res;
    }

    /* A scheduler yield is deliberately separate from poll(2).  poll() also
     * services the global TTY input path in XSCore, so background services
     * and synchronization primitives must be able to yield without touching
     * the interactive console. */
    if (number == AKAOS_SYSCALL_YIELD) {
        xscore_thread_yield();
        return 0;
    }
    if (number == AKAOS_SYSCALL_FUTEX) {
        return (uint64_t)syscall_futex(process, argument0, argument1,
                                       argument2);
    }

    if (number == AKAOS_SYSCALL_THREAD_CREATE) {
        if (!process || !argument0 || !argument1 ||
            !xscore_user_range_prepare(argument1, sizeof(uintptr_t), 1) ||
            !xscore_user_range_prepare(argument2, sizeof(uintptr_t), 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        uint64_t tid = xscore_thread_create_user(
            process, argument0, argument1, argument2);
        return tid ? tid : (uint64_t)-AKAOS_EAGAIN;
    }
    if (number == AKAOS_SYSCALL_THREAD_EXIT) {
        if (!xscore_thread_current_is_user_mode()) {
            return (uint64_t)-AKAOS_EINVAL;
        }
        xscore_thread_record_user_return(argument0);
        return XSCORE_SYSCALL_EXIT_TO_KERNEL;
    }
    if (number == AKAOS_SYSCALL_THREAD_JOIN) {
        uint64_t value = 0;
        int64_t result = xscore_thread_join(argument0, &value);
        if (result != 0) return (uint64_t)result;
        if (argument1 && !xscore_copy_to_user(argument1, &value,
                                               sizeof(value))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return 0;
    }
    if (number == AKAOS_SYSCALL_THREAD_DETACH) {
        return (uint64_t)xscore_thread_detach(argument0);
    }
    if (number == AKAOS_SYSCALL_GETTID) {
        return xscore_thread_current_id();
    }
    if (number == AKAOS_SYSCALL_THREAD_STATE) {
        return (uint64_t)xscore_thread_state(argument0);
    }
    if (number == AKAOS_SYSCALL_SET_TP) {
        /* The process entry thread is a kernel-created trampoline that
         * transitions into ring 3, so it is not marked as a
         * ``user_mode_thread`` in the scheduler table.  The syscall itself
         * is only reachable through a user trap; use the owning process as
         * the capability check so the initial thread and pthread-created
         * user threads can both install their ABI thread pointer. */
        if (!process) {
            return (uint64_t)-AKAOS_EINVAL;
        }
        xscore_thread_set_user_tp(argument0);
        return 0;
    }
    if (number == AKAOS_SYSCALL_GET_TP) {
        if (!process) {
            return (uint64_t)-AKAOS_EINVAL;
        }
        return xscore_thread_user_tp();
    }

    if (number == AKAOS_SYSCALL_READ) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        if (argument2 == 0) return 0;
        if (argument2 > AKAOS_SYSCALL_MAX_IO)
            argument2 = AKAOS_SYSCALL_MAX_IO;
        if (!xscore_user_range_prepare(argument1, (size_t)argument2, 1))
            return (uint64_t)-AKAOS_EFAULT;
        void *buffer = xscore_heap_allocate((size_t)argument2);
        if (!buffer) return (uint64_t)-AKAOS_ENOMEM;
        int64_t result = xscore_fd_read(table, (int)argument0, buffer, argument2);
        if (result > 0 &&
            !xscore_copy_to_user(argument1, buffer, (size_t)result)) {
            result = -AKAOS_EFAULT;
        }
        (void)xscore_heap_free(buffer);
        if (process) {
            uint64_t sig_res = check_and_deliver_pending_signals(process);
            if (sig_res != 0) return sig_res;
        }
        return (uint64_t)result;
    }
    if (number == AKAOS_SYSCALL_WRITE) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        if (argument2 == 0) return 0;
        if (argument2 > AKAOS_SYSCALL_MAX_IO)
            argument2 = AKAOS_SYSCALL_MAX_IO;
        void *buffer = xscore_heap_allocate((size_t)argument2);
        if (!buffer) return (uint64_t)-AKAOS_ENOMEM;
        if (!xscore_copy_from_user(buffer, argument1, (size_t)argument2)) {
            (void)xscore_heap_free(buffer);
            return (uint64_t)-AKAOS_EFAULT;
        }
        int64_t result = xscore_fd_write(table, (int)argument0, buffer,
                                         argument2);
        (void)xscore_heap_free(buffer);
        if (process) {
            uint32_t signal = 0;
            if (xscore_process_take_pending_signal(&signal)) {
                xscore_process_record_termination_signal(signal);
                xscore_thread_record_user_exit(UINT64_C(128) + signal);
                return XSCORE_SYSCALL_EXIT_TO_KERNEL;
            }
        }
        return (uint64_t)result;
    }
    if (number == AKAOS_SYSCALL_CLOSE) {
        return table ? (uint64_t)xscore_fd_close(table, (int)argument0)
                     : (uint64_t)-AKAOS_EBADF;
    }
    if (number == AKAOS_SYSCALL_LSEEK) {
        if (!table || argument2 > AKAOS_SEEK_END) return (uint64_t)-AKAOS_EINVAL;
        int64_t seek_offset = (int64_t)argument1;
#if defined(__i386__) || defined(__arm__) || \
    (defined(__riscv) && __riscv_xlen == 32)
        /* 32-bit syscall entry stubs carry scalar arguments in one word. */
        seek_offset = (int64_t)(int32_t)argument1;
#endif
        return (uint64_t)xscore_fd_seek(table, (int)argument0,
                                        seek_offset,
                                        (int)argument2);
    }
    if (number == AKAOS_SYSCALL_DUP) {
        return table ? (uint64_t)xscore_fd_dup(table, (int)argument0)
                     : (uint64_t)-AKAOS_EBADF;
    }
    if (number == AKAOS_SYSCALL_DUP2) {
        return table ? (uint64_t)xscore_fd_dup2(table, (int)argument0,
                                                (int)argument1)
                     : (uint64_t)-AKAOS_EBADF;
    }
    if (number == AKAOS_SYSCALL_FCNTL) {
        return table ? (uint64_t)xscore_fd_fcntl(
                           table, (int)argument0, (int)argument1, argument2)
                     : (uint64_t)-AKAOS_EBADF;
    }
    if (number == AKAOS_SYSCALL_FLOCK) {
        return table ? (uint64_t)xscore_fd_flock(
                           table, (int)argument0, (int)argument1)
                     : (uint64_t)-AKAOS_EBADF;
    }
    if (number == AKAOS_SYSCALL_SIGPROCMASK) {
        if (!process) return (uint64_t)-AKAOS_EINVAL;
        uint32_t mask = 0U;
        uint32_t old_mask = 0U;
        if (argument1 != 0U &&
            (!xscore_user_range_prepare(argument1, sizeof(mask), 0) ||
             !xscore_copy_from_user(&mask, argument1, sizeof(mask))))
            return (uint64_t)-AKAOS_EFAULT;
        if (argument2 != 0U &&
            !xscore_user_range_prepare(argument2, sizeof(old_mask), 1))
            return (uint64_t)-AKAOS_EFAULT;
        if (argument1 == 0U && argument2 == 0U) return 0U;
        int64_t result = xscore_process_update_signal_mask(
            argument1 == 0U ? UINT32_MAX : (uint32_t)argument0,
            mask, &old_mask);
        if (result < 0) return (uint64_t)result;
        if (argument2 != 0U &&
            !xscore_copy_to_user(argument2, &old_mask, sizeof(old_mask)))
            return (uint64_t)-AKAOS_EFAULT;
        return 0U;
    }
    if (number == AKAOS_SYSCALL_SIGNALFD) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        uint32_t signal_mask = 0U;
        if (argument1 == 0U ||
            !xscore_user_range_prepare(argument1, sizeof(signal_mask), 0) ||
            !xscore_copy_from_user(&signal_mask, argument1,
                                   sizeof(signal_mask))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return (uint64_t)xscore_fd_signalfd(
            table, (int32_t)(uint32_t)argument0, signal_mask,
            (uint32_t)argument2);
    }
    if (number == AKAOS_SYSCALL_OPEN) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        if (argument1 == 0 || argument1 > AKAOS_SYSCALL_PATH_MAX) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        char path[AKAOS_SYSCALL_PATH_MAX + 1];
        if (!xscore_copy_from_user(path, argument0, (size_t)argument1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        path[argument1] = 0;
        enum xscore_device_kind device =
            xscore_vfs_device_kind_at(cwd_node, path);
        if (device != XSCORE_DEVICE_NONE) {
            int error = authorize_device_open(cwd_node, path, argument2);
            if (error != 0) return (uint64_t)(int64_t)error;
        }
        if (device == XSCORE_DEVICE_TTY1 || device == XSCORE_DEVICE_NULL ||
            device == XSCORE_DEVICE_ZERO || device == XSCORE_DEVICE_RANDOM) {
            return (uint64_t)open_character_device(table, device, argument2);
        }
        if (device == XSCORE_DEVICE_INPUT_EVENT0) {
            if (!process || xscore_process_effective_uid(process) != 0) {
                return (uint64_t)-AKAOS_EACCES;
            }
            if ((argument2 & UINT64_C(3)) != AKAOS_O_RDONLY) {
                return (uint64_t)-AKAOS_EACCES;
            }
            return (uint64_t)xscore_fd_open_input(table);
        }
        if (device == XSCORE_DEVICE_FRAMEBUFFER) {
            if (!process || xscore_process_effective_uid(process) != 0) {
                return (uint64_t)-AKAOS_EACCES;
            }
            uint64_t access = argument2 & UINT64_C(3);
            if (access > AKAOS_O_RDWR) return (uint64_t)-AKAOS_EINVAL;
            return (uint64_t)xscore_fd_open_framebuffer(
                table, access != AKAOS_O_WRONLY,
                access != AKAOS_O_RDONLY);
        }
        if (device == XSCORE_DEVICE_PTMX) {
            if ((argument2 & UINT64_C(3)) != AKAOS_O_RDWR) {
                return (uint64_t)-AKAOS_EINVAL;
            }
            return (uint64_t)xscore_fd_open_ptmx(table);
        }
        if (device == XSCORE_DEVICE_AUDIO) {
            uint64_t access = argument2 & UINT64_C(3);
            if (access > AKAOS_O_RDWR) return (uint64_t)-AKAOS_EINVAL;
            return (uint64_t)xscore_fd_open_audio(
                table, access != AKAOS_O_WRONLY,
                access != AKAOS_O_RDONLY);
        }
        if (device == XSCORE_DEVICE_NET) {
            uint64_t access = argument2 & UINT64_C(3);
            if (access > AKAOS_O_RDWR) return (uint64_t)-AKAOS_EINVAL;
            return (uint64_t)xscore_fd_open_net(
                table, access != AKAOS_O_WRONLY,
                access != AKAOS_O_RDONLY);
        }
        if (device == XSCORE_DEVICE_PTS) {
            uint32_t pty_number = 0, slave_node = 0;
            if (!xscore_vfs_device_number_at(cwd_node, path,
                                              &pty_number, &slave_node)) {
                return (uint64_t)-AKAOS_ENODEV;
            }
            uint64_t access = argument2 & UINT64_C(3);
            if (access > AKAOS_O_RDWR) return (uint64_t)-AKAOS_EINVAL;
            return (uint64_t)xscore_fd_open_pts(
                table, pty_number, slave_node, access != AKAOS_O_WRONLY,
                access != AKAOS_O_RDONLY);
        }
        return (uint64_t)xscore_vfs_open_at(table, cwd_node, path, argument2, 0644);
    }
    if (number == AKAOS_SYSCALL_OPENAT) {
        if (!table ||
            !xscore_user_range_prepare(argument0,
                                       sizeof(struct akaos_openat_args), 0)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        struct akaos_openat_args args;
        if (!xscore_copy_from_user(&args, argument0, sizeof(args)) ||
            args.path == 0U) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        char path[AKAOS_SYSCALL_PATH_MAX + 1];
        if (!xscore_copy_string_from_user(path, sizeof(path), args.path, 0)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        size_t path_length = 0U;
        while (path_length < sizeof(path) && path[path_length])
            ++path_length;
        if (path_length == 0U || path_length >= sizeof(path))
            return (uint64_t)-AKAOS_ENAMETOOLONG;

        uint32_t open_cwd = cwd_node;
        if (args.directory != AKAOS_AT_FDCWD && path[0] != '/') {
            if (!xscore_fd_directory_state(table, args.directory,
                                           &open_cwd, 0)) {
                uint32_t ignored_node = 0U;
                if (!xscore_fd_vnode_state(table, args.directory,
                                           &ignored_node, 0, 0))
                    return (uint64_t)-AKAOS_EBADF;
                return (uint64_t)-AKAOS_ENOTDIR;
            }
        }

        if (args.flags & AKAOS_OPEN_DIRECTORY) {
            struct akaos_stat status;
            int stat_error = xscore_vfs_stat_at(open_cwd, path, &status);
            if (stat_error != 0) return (uint64_t)(int64_t)stat_error;
            if ((status.mode & UINT32_C(0170000)) != AKAOS_MODE_DIRECTORY)
                return (uint64_t)-AKAOS_ENOTDIR;
        }

        enum xscore_device_kind device =
            xscore_vfs_device_kind_at(open_cwd, path);
        if (device != XSCORE_DEVICE_NONE) {
            int error = authorize_device_open(open_cwd, path, args.flags);
            if (error != 0) return (uint64_t)(int64_t)error;
        }
        if (device == XSCORE_DEVICE_TTY1 || device == XSCORE_DEVICE_NULL ||
            device == XSCORE_DEVICE_ZERO || device == XSCORE_DEVICE_RANDOM) {
            return (uint64_t)open_character_device(table, device, args.flags);
        }
        if (device == XSCORE_DEVICE_INPUT_EVENT0) {
            if (!process || xscore_process_effective_uid(process) != 0)
                return (uint64_t)-AKAOS_EACCES;
            if ((args.flags & UINT32_C(3)) != AKAOS_O_RDONLY)
                return (uint64_t)-AKAOS_EACCES;
            return (uint64_t)xscore_fd_open_input(table);
        }
        if (device == XSCORE_DEVICE_FRAMEBUFFER) {
            if (!process || xscore_process_effective_uid(process) != 0)
                return (uint64_t)-AKAOS_EACCES;
            uint64_t access = args.flags & UINT32_C(3);
            if (access > AKAOS_O_RDWR) return (uint64_t)-AKAOS_EINVAL;
            return (uint64_t)xscore_fd_open_framebuffer(
                table, access != AKAOS_O_WRONLY, access != AKAOS_O_RDONLY);
        }
        if (device == XSCORE_DEVICE_PTMX) {
            if ((args.flags & UINT32_C(3)) != AKAOS_O_RDWR)
                return (uint64_t)-AKAOS_EINVAL;
            return (uint64_t)xscore_fd_open_ptmx(table);
        }
        if (device == XSCORE_DEVICE_AUDIO || device == XSCORE_DEVICE_NET) {
            uint64_t access = args.flags & UINT32_C(3);
            if (access > AKAOS_O_RDWR) return (uint64_t)-AKAOS_EINVAL;
            if (device == XSCORE_DEVICE_AUDIO)
                return (uint64_t)xscore_fd_open_audio(
                    table, access != AKAOS_O_WRONLY,
                    access != AKAOS_O_RDONLY);
            return (uint64_t)xscore_fd_open_net(
                table, access != AKAOS_O_WRONLY,
                access != AKAOS_O_RDONLY);
        }
        if (device == XSCORE_DEVICE_PTS) {
            uint32_t pty_number = 0U, slave_node = 0U;
            if (!xscore_vfs_device_number_at(open_cwd, path,
                                              &pty_number, &slave_node))
                return (uint64_t)-AKAOS_ENODEV;
            uint64_t access = args.flags & UINT32_C(3);
            if (access > AKAOS_O_RDWR) return (uint64_t)-AKAOS_EINVAL;
            return (uint64_t)xscore_fd_open_pts(
                table, pty_number, slave_node, access != AKAOS_O_WRONLY,
                access != AKAOS_O_RDONLY);
        }
        return (uint64_t)xscore_vfs_open_at(table, open_cwd, path,
                                            args.flags, args.mode);
    }
    if (number == AKAOS_SYSCALL_UNLINKAT) {
        if (!table ||
            !xscore_user_range_prepare(argument0,
                                       sizeof(struct akaos_unlinkat_args), 0))
            return (uint64_t)-AKAOS_EFAULT;
        struct akaos_unlinkat_args args;
        if (!xscore_copy_from_user(&args, argument0, sizeof(args)) ||
            args.path == 0U || (args.flags & ~AKAOS_AT_REMOVEDIR) != 0U)
            return (uint64_t)-AKAOS_EFAULT;
        char path[AKAOS_SYSCALL_PATH_MAX + 1];
        if (!xscore_copy_string_from_user(path, sizeof(path), args.path, 0))
            return (uint64_t)-AKAOS_EFAULT;
        size_t path_length = 0U;
        while (path_length < sizeof(path) && path[path_length])
            ++path_length;
        if (path_length == 0U || path_length >= sizeof(path))
            return (uint64_t)-AKAOS_ENAMETOOLONG;
        uint32_t unlink_cwd = cwd_node;
        if (args.directory != AKAOS_AT_FDCWD && path[0] != '/') {
            if (!xscore_fd_directory_state(table, args.directory,
                                           &unlink_cwd, 0)) {
                uint32_t ignored_node = 0U;
                if (!xscore_fd_vnode_state(table, args.directory,
                                           &ignored_node, 0, 0))
                    return (uint64_t)-AKAOS_EBADF;
                return (uint64_t)-AKAOS_ENOTDIR;
            }
        }
        return (uint64_t)(int64_t)xscore_vfs_unlink_at(unlink_cwd, path);
    }
    if (number == AKAOS_SYSCALL_MKDIRAT) {
        if (!table ||
            !xscore_user_range_prepare(argument0,
                                       sizeof(struct akaos_mkdirat_args), 0))
            return (uint64_t)-AKAOS_EFAULT;
        struct akaos_mkdirat_args args;
        if (!xscore_copy_from_user(&args, argument0, sizeof(args)))
            return (uint64_t)-AKAOS_EFAULT;
        char path[AKAOS_SYSCALL_PATH_MAX + 1U];
        int path_error = copy_at_path_from_user(path, args.path);
        if (path_error != 0) return (uint64_t)(int64_t)path_error;
        uint32_t mkdir_cwd = cwd_node;
        int directory_error = resolve_at_directory(
            table, cwd_node, args.directory, path, &mkdir_cwd);
        if (directory_error != 0)
            return (uint64_t)(int64_t)directory_error;
        return (uint64_t)(int64_t)xscore_vfs_mkdir_at(
            mkdir_cwd, path, args.mode);
    }
    if (number == AKAOS_SYSCALL_RENAMEAT) {
        if (!table ||
            !xscore_user_range_prepare(argument0,
                                       sizeof(struct akaos_renameat_args), 0))
            return (uint64_t)-AKAOS_EFAULT;
        struct akaos_renameat_args args;
        if (!xscore_copy_from_user(&args, argument0, sizeof(args)))
            return (uint64_t)-AKAOS_EFAULT;
        char old_path[AKAOS_SYSCALL_PATH_MAX + 1U];
        char new_path[AKAOS_SYSCALL_PATH_MAX + 1U];
        int path_error = copy_at_path_from_user(old_path, args.old_path);
        if (path_error != 0) return (uint64_t)(int64_t)path_error;
        path_error = copy_at_path_from_user(new_path, args.new_path);
        if (path_error != 0) return (uint64_t)(int64_t)path_error;
        uint32_t old_cwd = cwd_node;
        uint32_t new_cwd = cwd_node;
        int directory_error = resolve_at_directory(
            table, cwd_node, args.old_directory, old_path, &old_cwd);
        if (directory_error != 0)
            return (uint64_t)(int64_t)directory_error;
        directory_error = resolve_at_directory(
            table, cwd_node, args.new_directory, new_path, &new_cwd);
        if (directory_error != 0)
            return (uint64_t)(int64_t)directory_error;
        return (uint64_t)(int64_t)xscore_vfs_rename_at(
            old_cwd, old_path, new_cwd, new_path);
    }
    if (number == AKAOS_SYSCALL_STAT) {
        if (argument1 == 0 || argument1 > AKAOS_SYSCALL_PATH_MAX ||
            !xscore_user_range_prepare(argument2, sizeof(struct akaos_stat), 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        char path[AKAOS_SYSCALL_PATH_MAX + 1];
        if (!xscore_copy_from_user(path, argument0, (size_t)argument1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        path[argument1] = 0;
        struct akaos_stat status;
        int err = xscore_vfs_stat_at(cwd_node, path, &status);
        if (err != 0) return (uint64_t)(int64_t)err;
        if (!xscore_copy_to_user(argument2, &status, sizeof(status))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return 0;
    }
    if (number == AKAOS_SYSCALL_FSTAT) {
        if (!table ||
            !xscore_user_range_prepare(argument1, sizeof(struct akaos_stat), 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        uint32_t shm_object_id = 0U;
        if (xscore_fd_shm_state(table, (int)argument0, &shm_object_id,
                                0, 0)) {
            struct akaos_stat status;
            int64_t err = xscore_process_shm_stat(shm_object_id, &status);
            if (err != 0) return (uint64_t)err;
            if (!xscore_copy_to_user(argument1, &status, sizeof(status))) {
                return (uint64_t)-AKAOS_EFAULT;
            }
            return 0;
        }
        uint32_t node_id = 0U;
        int is_vnode = xscore_fd_vnode_state(table, (int)argument0,
                                             &node_id, 0, 0);
        if (!is_vnode &&
            !xscore_fd_directory_state(table, (int)argument0, &node_id, 0)) {
            return (uint64_t)-AKAOS_EBADF;
        }
        struct akaos_stat status;
        int err = xscore_vfs_stat_node_id(node_id, &status);
        if (err != 0) return (uint64_t)(int64_t)err;
        if (!xscore_copy_to_user(argument1, &status, sizeof(status))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return 0;
    }
    if (number == AKAOS_SYSCALL_GETDENTS) {
        if (!table || argument2 == 0 ||
            argument2 > AKAOS_SYSCALL_MAX_IO / sizeof(struct akaos_dirent)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        size_t bytes = (size_t)argument2 * sizeof(struct akaos_dirent);
        if (!xscore_user_range_prepare(argument1, bytes, 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        struct akaos_dirent *entries = xscore_heap_allocate(bytes);
        if (!entries) return (uint64_t)-AKAOS_ENOMEM;
        int64_t result = xscore_vfs_getdents(table, (int)argument0, entries,
                                             argument2);
        if (result > 0 && !xscore_copy_to_user(
                              argument1, entries,
                              (size_t)result * sizeof(struct akaos_dirent))) {
            result = -AKAOS_EFAULT;
        }
        (void)xscore_heap_free(entries);
        return (uint64_t)result;
    }
    if (number == AKAOS_SYSCALL_GETCWD) {
        if (argument1 == 0 || argument1 > AKAOS_SYSCALL_MAX_IO ||
            !xscore_user_range_prepare(argument0, (size_t)argument1, 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        char cwd[128];
        size_t len = xscore_process_copy_cwd(process, cwd, sizeof(cwd));
        if (len + 1 > argument1) return (uint64_t)-AKAOS_EINVAL;
        if (!xscore_copy_to_user(argument0, cwd, len + 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return argument0;
    }
    if (number == AKAOS_SYSCALL_CHDIR) {
        if (argument1 == 0 || argument1 > AKAOS_SYSCALL_PATH_MAX) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        char path[AKAOS_SYSCALL_PATH_MAX + 1];
        if (!xscore_copy_from_user(path, argument0, (size_t)argument1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        path[argument1] = 0;
        return process ? (uint64_t)(int64_t)xscore_process_set_cwd(process, path)
                       : (uint64_t)-AKAOS_EINVAL;
    }
    if (number == AKAOS_SYSCALL_FCHDIR) {
        if (!table || !process) return (uint64_t)-AKAOS_EBADF;
        uint32_t directory_node = 0U;
        if (!xscore_fd_directory_state(table, (int)argument0,
                                       &directory_node, 0)) {
            uint32_t ignored_node = 0U;
            if (!xscore_fd_vnode_state(table, (int)argument0,
                                       &ignored_node, 0, 0))
                return (uint64_t)-AKAOS_EBADF;
            return (uint64_t)-AKAOS_ENOTDIR;
        }
        return (uint64_t)(int64_t)xscore_process_set_cwd_node(
            process, directory_node);
    }
    if (number == AKAOS_SYSCALL_MKDIR) {
        if (argument1 == 0 || argument1 > AKAOS_SYSCALL_PATH_MAX) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        char path[AKAOS_SYSCALL_PATH_MAX + 1];
        if (!xscore_copy_from_user(path, argument0, (size_t)argument1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        path[argument1] = 0;
        return (uint64_t)(int64_t)xscore_vfs_mkdir_at(cwd_node, path, (uint32_t)argument2);
    }
    if (number == AKAOS_SYSCALL_UNLINK) {
        if (argument1 == 0 || argument1 > AKAOS_SYSCALL_PATH_MAX) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        char path[AKAOS_SYSCALL_PATH_MAX + 1];
        if (!xscore_copy_from_user(path, argument0, (size_t)argument1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        path[argument1] = 0;
        return (uint64_t)(int64_t)xscore_vfs_unlink_at(cwd_node, path);
    }
    if (number == AKAOS_SYSCALL_SPAWN) {
        if (argument2 != 0 &&
            !xscore_user_range_prepare(argument2, sizeof(uint64_t), 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        char kpath[128];
        if (!xscore_copy_string_from_user(kpath, sizeof(kpath), argument0, 0))
            return (uint64_t)-AKAOS_EFAULT;

        char kargv_storage[16][128];
        const char *kargv[17];
        int argc = 0;

        if (argument1 != 0) {
            while (argc < 16) {
                uintptr_t arg_ptr = 0;
                if (!xscore_copy_from_user(
                        &arg_ptr,
                        argument1 + (uint64_t)argc * sizeof(uintptr_t),
                        sizeof(arg_ptr))) {
                    return (uint64_t)-AKAOS_EFAULT;
                }
                if (!arg_ptr) break;
                if (!xscore_copy_string_from_user(
                        kargv_storage[argc], sizeof(kargv_storage[argc]),
                        (uint64_t)arg_ptr, 0)) {
                    return (uint64_t)-AKAOS_EFAULT;
                }
                kargv[argc] = kargv_storage[argc];
                ++argc;
            }
        }
        kargv[argc] = 0;

        uint64_t child_pid = 0;
        int64_t res = xscore_process_spawn_path(kpath, argc, kargv, &child_pid);
        if (res < 0) return (uint64_t)res;

        if (argument2 != 0) {
            if (!xscore_copy_to_user(argument2, &child_pid,
                                     sizeof(child_pid))) {
                return (uint64_t)-AKAOS_EFAULT;
            }
        }
        return (uint64_t)child_pid;
    }
    if (number == AKAOS_SYSCALL_SPAWNVE) {
        struct akaos_spawnve_args arguments;
        if (!xscore_copy_from_user(&arguments, argument0,
                                   sizeof(arguments))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        if (arguments.out_pid != 0 &&
            !xscore_user_range_prepare(arguments.out_pid,
                                       sizeof(uint64_t), 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }

        char kpath[128];
        if (!xscore_copy_string_from_user(kpath, sizeof(kpath),
                                          arguments.path, 0)) {
            return (uint64_t)-AKAOS_EFAULT;
        }

        char kargv_storage[16][128];
        const char *kargv[17];
        int argc = 0;
        if (arguments.argv != 0) {
            while (argc < 16) {
                uintptr_t pointer = 0;
                if (!xscore_copy_from_user(
                        &pointer,
                        arguments.argv + (uint64_t)argc * sizeof(uintptr_t),
                        sizeof(pointer))) {
                    return (uint64_t)-AKAOS_EFAULT;
                }
                if (!pointer) break;
                if (!xscore_copy_string_from_user(
                        kargv_storage[argc], sizeof(kargv_storage[argc]),
                        (uint64_t)pointer, 0)) {
                    return (uint64_t)-AKAOS_EFAULT;
                }
                kargv[argc] = kargv_storage[argc];
                ++argc;
            }
        }
        kargv[argc] = 0;

        char kenv_storage[16][128];
        const char *kenvp[17];
        int envc = 0;
        if (arguments.envp != 0) {
            while (envc < 16) {
                uintptr_t pointer = 0;
                if (!xscore_copy_from_user(
                        &pointer,
                        arguments.envp + (uint64_t)envc * sizeof(uintptr_t),
                        sizeof(pointer))) {
                    return (uint64_t)-AKAOS_EFAULT;
                }
                if (!pointer) break;
                if (!xscore_copy_string_from_user(
                        kenv_storage[envc], sizeof(kenv_storage[envc]),
                        (uint64_t)pointer, 0)) {
                    return (uint64_t)-AKAOS_EFAULT;
                }
                kenvp[envc] = kenv_storage[envc];
                ++envc;
            }
        }
        kenvp[envc] = 0;

        if (arguments.action_count > 8U ||
            (arguments.action_count != 0 && arguments.actions == 0)) {
            return (uint64_t)-AKAOS_EINVAL;
        }
        struct akaos_spawn_file_action kactions[8];
        for (uint32_t index = 0; index < arguments.action_count; ++index) {
            if (!xscore_copy_from_user(
                    &kactions[index],
                    arguments.actions + (uint64_t)index * sizeof(kactions[0]),
                    sizeof(kactions[0]))) {
                return (uint64_t)-AKAOS_EFAULT;
            }
        }

        uint64_t child_pid = 0;
        int64_t res = xscore_process_spawn_path_env(
            kpath, argc, kargv, envc, kenvp, kactions,
            arguments.action_count, arguments.spawn_flags,
            arguments.process_group, arguments.signal_mask,
            arguments.signal_default, &child_pid);
        if (res < 0) return (uint64_t)res;
        if (arguments.out_pid != 0 &&
            !xscore_copy_to_user(arguments.out_pid, &child_pid,
                                 sizeof(child_pid))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return (uint64_t)child_pid;
    }
    if (number == AKAOS_SYSCALL_EXECVE) {
        char kpath[256];
        if (!xscore_copy_string_from_user(kpath, sizeof(kpath), argument0, 0)) {
            return (uint64_t)-AKAOS_EFAULT;
        }

        char (*kargv_storage)[256] = xscore_heap_allocate(32 * 256);
        char (*kenv_storage)[256] = xscore_heap_allocate(32 * 256);
        if (!kargv_storage || !kenv_storage) {
            if (kargv_storage) (void)xscore_heap_free(kargv_storage);
            if (kenv_storage) (void)xscore_heap_free(kenv_storage);
            return (uint64_t)-AKAOS_ENOMEM;
        }

        const char *kargv[33];
        int argc = 0;
        if (argument1 != 0) {
            while (argc < 32) {
                uintptr_t pointer = 0;
                if (!xscore_copy_from_user(
                        &pointer,
                        argument1 + (uint64_t)argc * sizeof(uintptr_t),
                        sizeof(pointer))) {
                    (void)xscore_heap_free(kargv_storage);
                    (void)xscore_heap_free(kenv_storage);
                    return (uint64_t)-AKAOS_EFAULT;
                }
                if (!pointer) break;
                if (!xscore_copy_string_from_user(
                        kargv_storage[argc], 256,
                        (uint64_t)pointer, 0)) {
                    (void)xscore_heap_free(kargv_storage);
                    (void)xscore_heap_free(kenv_storage);
                    return (uint64_t)-AKAOS_EFAULT;
                }
                kargv[argc] = kargv_storage[argc];
                ++argc;
            }
        }
        kargv[argc] = 0;

        const char *kenvp[33];
        int envc = 0;
        if (argument2 != 0) {
            while (envc < 32) {
                uintptr_t pointer = 0;
                if (!xscore_copy_from_user(
                        &pointer,
                        argument2 + (uint64_t)envc * sizeof(uintptr_t),
                        sizeof(pointer))) {
                    (void)xscore_heap_free(kargv_storage);
                    (void)xscore_heap_free(kenv_storage);
                    return (uint64_t)-AKAOS_EFAULT;
                }
                if (!pointer) break;
                if (!xscore_copy_string_from_user(
                        kenv_storage[envc], 256,
                        (uint64_t)pointer, 0)) {
                    (void)xscore_heap_free(kargv_storage);
                    (void)xscore_heap_free(kenv_storage);
                    return (uint64_t)-AKAOS_EFAULT;
                }
                kenvp[envc] = kenv_storage[envc];
                ++envc;
            }
        }
        kenvp[envc] = 0;
        int64_t ret = xscore_process_execve(kpath, argc, kargv, envc, kenvp);
        (void)xscore_heap_free(kargv_storage);
        (void)xscore_heap_free(kenv_storage);
        return (uint64_t)ret;
    }
    if (number == AKAOS_SYSCALL_WAITPID) {
        if (argument1 != 0 &&
            !xscore_user_range_prepare(argument1, sizeof(int), 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        uint64_t status = 0;
        int64_t res = xscore_process_waitpid_options(argument0, &status, (uint32_t)argument2);
        if (res < 0) return (uint64_t)res;

        if (argument1 != 0) {
            int user_status = (int)status;
            if (!xscore_copy_to_user(argument1, &user_status,
                                     sizeof(user_status))) {
                return (uint64_t)-AKAOS_EFAULT;
            }
        }
        return (uint64_t)res;
    }
    if (number == AKAOS_SYSCALL_BRK) {
        return process ? xscore_process_brk(process, argument0) : 0;
    }
    if (number == AKAOS_SYSCALL_MMAP) {
        struct akaos_mmap_args arguments;
        if (!xscore_copy_from_user(&arguments, argument0,
                                   sizeof(arguments))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return (uint64_t)xscore_process_mmap(&arguments);
    }
    if (number == AKAOS_SYSCALL_MUNMAP) {
        return (uint64_t)xscore_process_munmap(argument0, argument1);
    }
    if (number == AKAOS_SYSCALL_MPROTECT) {
        return (uint64_t)xscore_process_mprotect(argument0, argument1,
                                                 argument2);
    }
    if (number == AKAOS_SYSCALL_SHM_OPEN) {
        struct akaos_shm_open_args arguments;
        if (!xscore_copy_from_user(&arguments, argument0,
                                   sizeof(arguments))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        if (arguments.name_length == 0 ||
            arguments.name_length > AKAOS_SHM_NAME_MAX) {
            return (uint64_t)-AKAOS_EINVAL;
        }
        char name[AKAOS_SHM_NAME_MAX + 1];
        if (!xscore_copy_from_user(name, arguments.name,
                                   (size_t)arguments.name_length)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        name[arguments.name_length] = 0;
        return (uint64_t)xscore_process_shm_open(
            name, arguments.flags, arguments.mode);
    }
    if (number == AKAOS_SYSCALL_MEMFD_CREATE) {
        struct akaos_memfd_create_args arguments;
        if (!xscore_copy_from_user(&arguments, argument0,
                                   sizeof(arguments))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        if (arguments.name_length > AKAOS_MEMFD_NAME_MAX ||
            (arguments.flags &
             ~(AKAOS_MFD_CLOEXEC | AKAOS_MFD_ALLOW_SEALING)) != 0U) {
            return (uint64_t)-AKAOS_EINVAL;
        }
        char name[AKAOS_MEMFD_NAME_MAX + 1U];
        if (arguments.name_length != 0U &&
            !xscore_copy_from_user(name, arguments.name,
                                   (size_t)arguments.name_length)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        name[arguments.name_length] = 0;
        return (uint64_t)xscore_process_memfd_create(
            name, arguments.name_length, arguments.flags);
    }
    if (number == AKAOS_SYSCALL_SHM_UNLINK) {
        if (argument1 == 0 || argument1 > AKAOS_SHM_NAME_MAX) {
            return (uint64_t)-AKAOS_EINVAL;
        }
        char name[AKAOS_SHM_NAME_MAX + 1];
        if (!xscore_copy_from_user(name, argument0, (size_t)argument1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        name[argument1] = 0;
        return (uint64_t)xscore_process_shm_unlink(name);
    }
    if (number == AKAOS_SYSCALL_FTRUNCATE) {
        return (uint64_t)xscore_process_ftruncate((int)argument0, argument1);
    }
    if (number == AKAOS_SYSCALL_POLL) {
        if (!table || argument1 > AKAOS_POLL_MAX_FDS) {
            return (uint64_t)-AKAOS_EINVAL;
        }
        size_t bytes = (size_t)argument1 * sizeof(struct akaos_pollfd);
        if (bytes != 0 &&
            !xscore_user_range_prepare(argument0, bytes, 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        struct akaos_pollfd descriptors[AKAOS_POLL_MAX_FDS];
        if (bytes != 0 &&
            !xscore_copy_from_user(descriptors, argument0, bytes)) {
            return (uint64_t)-AKAOS_EFAULT;
        }

        int timeout = (int32_t)argument2;
        uint64_t deadline = 0;
        if (timeout > 0) {
            deadline = poll_clock_now() + poll_clock_delta(timeout);
        }

        for (;;) {
            xscore_tty_poll_keyboard();
            int ready_count = 0;
            for (uint64_t index = 0; index < argument1; ++index) {
                if (descriptors[index].fd < 0) {
                    descriptors[index].revents = 0;
                    continue;
                }
                descriptors[index].revents = xscore_fd_poll(
                    table, descriptors[index].fd,
                    descriptors[index].events);
                if (descriptors[index].revents != 0) ++ready_count;
            }
            if (ready_count != 0 || timeout == 0 ||
                (timeout > 0 &&
                 (int64_t)(poll_clock_now() - deadline) >= 0)) {
                if (bytes != 0 &&
                    !xscore_copy_to_user(argument0, descriptors, bytes)) {
                    return (uint64_t)-AKAOS_EFAULT;
                }
                return (uint64_t)ready_count;
            }
            if (xscore_process_signal_pending_current()) {
                return (uint64_t)-AKAOS_EINTR;
            }
#if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("sti; pause");
#elif defined(__aarch64__) || defined(__arm__)
            __asm__ volatile("yield");
#elif defined(__riscv)
            __asm__ volatile("nop");
#endif
            xscore_thread_yield();
        }
    }
    if (number == AKAOS_SYSCALL_PIPE) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        const uint64_t pipe_flags = AKAOS_O_CLOEXEC | AKAOS_O_NONBLOCK;
        if ((argument1 & ~pipe_flags) != 0U || argument2 != 0U) {
            return (uint64_t)-AKAOS_EINVAL;
        }
        if (!xscore_user_range_prepare(argument0, 2 * sizeof(int), 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        int read_fd = -1, write_fd = -1;
        int err = xscore_pipe_create(table, &read_fd, &write_fd,
                                     (uint32_t)argument1);
        if (err != 0) return (uint64_t)(int64_t)err;
        int user_fds[2] = {read_fd, write_fd};
        if (!xscore_copy_to_user(argument0, user_fds, sizeof(user_fds))) {
            (void)xscore_fd_close(table, read_fd);
            (void)xscore_fd_close(table, write_fd);
            return (uint64_t)-AKAOS_EFAULT;
        }
        return 0;
    }
    if (number == AKAOS_SYSCALL_KILL) {
        if (argument0 == 0 || argument1 < AKAOS_SIGNAL_MIN ||
            argument1 > AKAOS_SIGNAL_MAX) {
            return (uint64_t)-AKAOS_EINVAL;
        }
        int64_t res;
        if ((int64_t)argument0 < 0) {
            uint64_t target_pgid = (uint64_t)(-(int64_t)argument0);
            res = xscore_process_signal_pgrp(target_pgid, (uint32_t)argument1) ? 0 : -AKAOS_ESRCH;
        } else {
            res = xscore_process_signal(argument0, (uint32_t)argument1);
        }
        if (res == 0 && process) {
            uint64_t sig_res = check_and_deliver_pending_signals(process);
            if (sig_res != 0) return sig_res;
        }
        return (uint64_t)res;
    }
    if (number == AKAOS_SYSCALL_SYNC) {
        xscore_bufcache_sync();
        if (xscore_virtio_blk_present()) {
            xscore_vfs_persistence_sync();
        }
        return 0;
    }
    if (number == AKAOS_SYSCALL_REBOOT) {
        if (!process || xscore_process_effective_uid(process) != 0) {
            return (uint64_t)-AKAOS_EPERM;
        }
        xscore_bufcache_sync();
        if (xscore_virtio_blk_present()) {
            xscore_vfs_persistence_sync();
        }
        if (argument0 == AKAOS_RB_REBOOT) {
            xscore_serial_write("[INFO] System reboot requested\n");
            xscore_acpi_reboot();
#if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xfe), "Nd"((uint16_t)0x64));
            __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x06), "Nd"((uint16_t)0xcf9));
            struct {
                uint16_t limit;
                uint64_t base;
            } __attribute__((packed)) null_idt = {0, 0};
            __asm__ volatile("lidt %0; int3" : : "m"(null_idt));
            for (;;) __asm__ volatile("cli; hlt");
#elif defined(__aarch64__)
            /* PSCI 0.2 SYSTEM_RESET through QEMU virt's HVC conduit. */
            arch_psci_system_call(UINT64_C(0x84000009));
            for (;;) __asm__ volatile("wfe");
#elif defined(__arm__)
            /* PSCI 0.2 SYSTEM_RESET through QEMU virt's HVC conduit. */
            arch_psci_system_call(UINT32_C(0x84000009));
            for (;;) __asm__ volatile("wfi");
#elif defined(__riscv)
            /* SBI SRST cold reboot; OpenSBI owns the platform reset device. */
            arch_sbi_system_reset((uintptr_t)1);
            /* Compatibility fallback for QEMU virt without SBI SRST. */
            *(volatile uint32_t*)0x100000 = 0x7777;
            for (;;) __asm__ volatile("wfi");
#endif
        } else if (argument0 == AKAOS_RB_POWEROFF) {
            xscore_serial_write("[INFO] System poweroff requested\n");
            xscore_acpi_shutdown();
#if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
            __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xb004));
            __asm__ volatile("outw %0, %1" : : "a"((uint16_t)0x3400), "Nd"((uint16_t)0x4004));
            __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0x00), "Nd"((uint16_t)0xf4));
            for (;;) __asm__ volatile("cli; hlt");
#elif defined(__aarch64__)
            /* PSCI 0.2 SYSTEM_OFF through QEMU virt's HVC conduit. */
            arch_psci_system_call(UINT64_C(0x84000008));
            for (;;) __asm__ volatile("wfe");
#elif defined(__arm__)
            /* PSCI 0.2 SYSTEM_OFF through QEMU virt's HVC conduit. */
            arch_psci_system_call(UINT32_C(0x84000008));
            for (;;) __asm__ volatile("wfi");
#elif defined(__riscv)
            /* SBI SRST shutdown; OpenSBI owns the platform poweroff device. */
            arch_sbi_system_reset((uintptr_t)0);
            /* Compatibility fallback for QEMU virt without SBI SRST. */
            *(volatile uint32_t*)0x100000 = 0x5555;
            for (;;) __asm__ volatile("wfi");
#endif
        } else {
            xscore_serial_write("[INFO] System halted\n");
            for (;;) {
#if defined(__x86_64__) || defined(__i386__)
                __asm__ volatile("cli; hlt");
#elif defined(__aarch64__)
                __asm__ volatile("wfe");
#elif defined(__arm__) || defined(__riscv)
                __asm__ volatile("wfi");
#else
                __asm__ volatile("");
#endif
            }
        }
        return 0;
    }
    if (number == AKAOS_SYSCALL_FCHMOD || number == AKAOS_SYSCALL_FCHOWN) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        return (uint64_t)(int64_t)xscore_fd_metadata(table, (int)argument0,
            number == AKAOS_SYSCALL_FCHOWN, (uint32_t)argument1, (uint32_t)argument2);
    }
    if (number == AKAOS_SYSCALL_CHMOD) {
        char path[AKAOS_SYSCALL_PATH_MAX + 1];
        if (!xscore_copy_string_from_user(path, sizeof(path), argument0, 0)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return (uint64_t)(int64_t)xscore_vfs_chmod_at(cwd_node, path, (uint32_t)argument1);
    }
    if (number == AKAOS_SYSCALL_CHOWN) {
        char path[AKAOS_SYSCALL_PATH_MAX + 1];
        if (!xscore_copy_string_from_user(path, sizeof(path), argument0, 0)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return (uint64_t)(int64_t)xscore_vfs_chown_at(cwd_node, path, (uint32_t)argument1, (uint32_t)argument2);
    }
    if (number == AKAOS_SYSCALL_GETUID) {
        return process ? xscore_process_uid(process) : 0;
    }
    if (number == AKAOS_SYSCALL_GETEUID) {
        return process ? xscore_process_effective_uid(process) : 0;
    }
    if (number == AKAOS_SYSCALL_GETGID) {
        return process ? xscore_process_gid(process) : 0;
    }
    if (number == AKAOS_SYSCALL_GETEGID) {
        return process ? xscore_process_effective_gid(process) : 0;
    }
    if (number == AKAOS_SYSCALL_SETUID) {
        return process ? (uint64_t)(int64_t)xscore_process_set_uid(
                             process, (uint32_t)argument0)
                       : (uint64_t)-AKAOS_EINVAL;
    }
    if (number == AKAOS_SYSCALL_SETGID) {
        return process ? (uint64_t)(int64_t)xscore_process_set_gid(
                             process, (uint32_t)argument0)
                       : (uint64_t)-AKAOS_EINVAL;
    }
    if (number == AKAOS_SYSCALL_TTY_ECHO) {
        return (uint64_t)xscore_tty_set_echo(argument0 != 0);
    }
    if (number == AKAOS_SYSCALL_SIGNAL_MODE) {
        return process ? (uint64_t)(int64_t)xscore_process_set_signal_ignored(
                             process, (uint32_t)argument0, argument1 != 0)
                       : (uint64_t)-AKAOS_EINVAL;
    }
    if (number == AKAOS_SYSCALL_SETPGID) {
        return (uint64_t)(int64_t)xscore_process_setpgid(argument0, argument1);
    }
    if (number == AKAOS_SYSCALL_GETPGID) {
        return (uint64_t)(int64_t)xscore_process_getpgid(argument0);
    }
    if (number == AKAOS_SYSCALL_TCSETPGRP) {
        if (!table || !xscore_fd_is_tty(table, (int)argument0)) {
            return (uint64_t)-AKAOS_ENOTTY;
        }
        return (uint64_t)(int64_t)xscore_process_tcsetpgrp(argument1);
    }
    if (number == AKAOS_SYSCALL_TCGETPGRP) {
        if (!table || !xscore_fd_is_tty(table, (int)argument0)) {
            return (uint64_t)-AKAOS_ENOTTY;
        }
        uint64_t pgid = xscore_process_tcgetpgrp();
        return pgid ? pgid : (uint64_t)-AKAOS_ENOTTY;
    }
    if (number == AKAOS_SYSCALL_GETSID) {
        return (uint64_t)xscore_process_getsid(argument0);
    }
    if (number == AKAOS_SYSCALL_SETSID) {
        return (uint64_t)xscore_process_setsid();
    }
    if (number == AKAOS_SYSCALL_PTSNAME) {
        if (!table || argument2 < 11 || argument2 > AKAOS_SYSCALL_PATH_MAX ||
            !xscore_user_range_prepare(argument1, (size_t)argument2, 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        uint32_t pty_number = 0;
        if (!xscore_fd_pts_number(table, (int)argument0, &pty_number) ||
            pty_number > 9) return (uint64_t)-AKAOS_ENOTTY;
        char path[] = "/dev/pts/0";
        path[9] = (char)('0' + pty_number);
        if (!xscore_copy_to_user(argument1, path, sizeof(path))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return 0;
    }
    if (number == AKAOS_SYSCALL_IOCTL) {
        if (!table || argument2 == 0) return (uint64_t)-AKAOS_EFAULT;
        size_t size = 0;
        int input = 0;
        if (argument1 == AKAOS_TCGETS || argument1 == AKAOS_TCSETS) {
            size = sizeof(struct akaos_termios);
            input = argument1 == AKAOS_TCSETS;
        } else if (argument1 == AKAOS_TIOCGWINSZ ||
                   argument1 == AKAOS_TIOCSWINSZ) {
            size = sizeof(struct akaos_winsize);
            input = argument1 == AKAOS_TIOCSWINSZ;
        } else if (argument1 == AKAOS_TIOCGNAME) {
            size = AKAOS_TTY_NAME_MAX;
        } else if (argument1 == AKAOS_SIOCGIFSTATS) {
            size = sizeof(struct akaos_net_stats);
        } else if (argument1 == AKAOS_FIONREAD) {
            size = sizeof(int);
        } else {
            return (uint64_t)-AKAOS_EINVAL;
        }
        uint8_t value[64];
        if (input && !xscore_copy_from_user(value, argument2, size)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        int64_t result = xscore_fd_ioctl(table, (int)argument0,
                                         (uint32_t)argument1, value);
        if (result < 0) return (uint64_t)result;
        if (!input && !xscore_copy_to_user(argument2, value, size)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return 0;
    }
    if (number == AKAOS_SYSCALL_FORK) {
        uint64_t child_pid = 0;
        int64_t err = xscore_process_fork(&child_pid);
        if (err != 0) return (uint64_t)err;
        return child_pid;
    }
    if (number == AKAOS_SYSCALL_SIGACTION) {
        return (uint64_t)xscore_process_sigaction(
            (uint32_t)argument0,
            (const struct akaos_sigaction *)(uintptr_t)argument1,
            (struct akaos_sigaction *)(uintptr_t)argument2);
    }
    if (number == AKAOS_SYSCALL_SIGRETURN) {
        return (uint64_t)xscore_process_sigreturn();
    }
    if (number == AKAOS_SYSCALL_GETPID) {
        return process ? xscore_process_pid(process) : 0;
    }
    if (number == AKAOS_SYSCALL_GETPPID) {
        return process ? xscore_process_parent_pid(process) : 0;
    }
    if (number == AKAOS_SYSCALL_SOCKET) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        int64_t sock_id = xscore_socket_create((int)argument0, (int)argument1, (int)argument2);
        if (sock_id < 0) return (uint64_t)sock_id;
        int64_t fd = xscore_fd_open_socket(table, (int)sock_id);
        if (fd < 0) {
            xscore_socket_close((int)sock_id);
            return (uint64_t)fd;
        }
        return (uint64_t)fd;
    }
    if (number == AKAOS_SYSCALL_CONNECT) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        int sock_id = xscore_fd_socket_id(table, (int)argument0);
        if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;
        uint16_t family = 0;
        if (argument2 < sizeof(family) ||
            !xscore_copy_from_user(&family, argument1, sizeof(family))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        if (family == AKAOS_AF_UNIX) {
            struct akaos_sockaddr_un sun;
            int64_t copy_status = copy_sockaddr_un_from_user(
                &sun, argument1, argument2);
            if (copy_status < 0) return (uint64_t)copy_status;
            return (uint64_t)xscore_socket_connect_unix(sock_id, sun.sun_path);
        }
        if (family != AKAOS_AF_INET) return (uint64_t)-AKAOS_EINVAL;
        struct akaos_sockaddr_in sin;
        if (argument2 < sizeof(sin) || !xscore_copy_from_user(&sin, argument1, sizeof(sin))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        uint16_t port = (uint16_t)(((sin.sin_port >> 8) & 0xff) | ((sin.sin_port & 0xff) << 8));
        return (uint64_t)xscore_socket_connect(sock_id, sin.sin_addr, port);
    }
    if (number == AKAOS_SYSCALL_BIND) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        int sock_id = xscore_fd_socket_id(table, (int)argument0);
        if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;
        uint16_t family = 0;
        if (argument2 < sizeof(family) ||
            !xscore_copy_from_user(&family, argument1, sizeof(family))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        if (family == AKAOS_AF_UNIX) {
            struct akaos_sockaddr_un sun;
            int64_t copy_status = copy_sockaddr_un_from_user(
                &sun, argument1, argument2);
            if (copy_status < 0) return (uint64_t)copy_status;
            return (uint64_t)xscore_socket_bind_unix(sock_id, sun.sun_path);
        }
        if (family != AKAOS_AF_INET) return (uint64_t)-AKAOS_EINVAL;
        struct akaos_sockaddr_in sin;
        if (argument2 < sizeof(sin) || !xscore_copy_from_user(&sin, argument1, sizeof(sin))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        uint16_t port = (uint16_t)(((sin.sin_port >> 8) & 0xff) | ((sin.sin_port & 0xff) << 8));
        return (uint64_t)xscore_socket_bind(sock_id, sin.sin_addr, port);
    }
    if (number == AKAOS_SYSCALL_LISTEN) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        int sock_id = xscore_fd_socket_id(table, (int)argument0);
        if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;
        return (uint64_t)xscore_socket_listen(sock_id, (int)argument1);
    }
    if (number == AKAOS_SYSCALL_GETSOCKOPT) {
        struct akaos_getsockopt_args args;
        if (!table || !xscore_copy_from_user(&args, argument0,
                                               sizeof(args))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        if (args.sockfd > INT32_MAX) return (uint64_t)-AKAOS_EBADF;
        if (args.level != AKAOS_SOL_SOCKET ||
            args.option != AKAOS_SO_PEERCRED) {
            uint32_t user_length = 0U;
            if (!xscore_copy_from_user(&user_length, args.optlen,
                                       sizeof(user_length)) ||
                !xscore_user_range_prepare(args.optlen,
                                           sizeof(user_length), 1))
                return (uint64_t)-AKAOS_EFAULT;
            if (user_length < sizeof(uint32_t))
                return (uint64_t)-AKAOS_EINVAL;
            int sock_id = xscore_fd_socket_id(table, (int)args.sockfd);
            if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;
            uint32_t value = 0U;
            int64_t result = xscore_socket_get_option(
                sock_id, (int)args.level, (int)args.option, &value);
            if (result < 0) return (uint64_t)result;
            uint32_t actual_length = sizeof(value);
            if (!xscore_copy_to_user(args.optval, &value, sizeof(value)) ||
                !xscore_copy_to_user(args.optlen, &actual_length,
                                     sizeof(actual_length)))
                return (uint64_t)-AKAOS_EFAULT;
            return 0U;
        }
        if (!xscore_user_range_prepare(args.optlen, sizeof(uint32_t), 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        uint32_t user_length = 0;
        if (!xscore_copy_from_user(&user_length, args.optlen,
                                   sizeof(user_length))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        if (user_length < sizeof(struct akaos_ucred)) {
            uint32_t required = sizeof(struct akaos_ucred);
            if (!xscore_copy_to_user(args.optlen, &required,
                                     sizeof(required))) {
                return (uint64_t)-AKAOS_EFAULT;
            }
            return (uint64_t)-AKAOS_EINVAL;
        }
        if (!xscore_user_range_prepare(args.optval,
                                       sizeof(struct akaos_ucred), 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        int sock_id = xscore_fd_socket_id(table, (int)args.sockfd);
        if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;
        struct akaos_ucred credentials;
        int64_t result = xscore_socket_get_peer_credentials(sock_id,
                                                             &credentials);
        if (result < 0) return (uint64_t)result;
        uint32_t actual_length = sizeof(credentials);
        if (!xscore_copy_to_user(args.optval, &credentials,
                                 sizeof(credentials)) ||
            !xscore_copy_to_user(args.optlen, &actual_length,
                                 sizeof(actual_length))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return 0;
    }
    if (number == AKAOS_SYSCALL_SETSOCKOPT) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        struct akaos_setsockopt_args args;
        if (!xscore_copy_from_user(&args, argument0, sizeof(args)))
            return (uint64_t)-AKAOS_EFAULT;
        if (args.sockfd > INT32_MAX) return (uint64_t)-AKAOS_EBADF;
        int sock_id = xscore_fd_socket_id(table, (int)args.sockfd);
        if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;
        if (args.optlen != sizeof(uint32_t))
            return (uint64_t)-AKAOS_EOPNOTSUPP;
        uint32_t value = 0U;
        if (!xscore_copy_from_user(&value, args.optval, sizeof(value)))
            return (uint64_t)-AKAOS_EFAULT;
        return (uint64_t)xscore_socket_set_option(
            sock_id, (int)args.level, (int)args.option, value);
    }
    if (number == AKAOS_SYSCALL_SHUTDOWN) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        int sock_id = xscore_fd_socket_id(table, (int)argument0);
        if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;
        return (uint64_t)xscore_socket_shutdown(sock_id, (int)argument1);
    }
    if (number == AKAOS_SYSCALL_ACCEPT) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        int sock_id = xscore_fd_socket_id(table, (int)argument0);
        if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;
        uint32_t r_ip = 0; uint16_t r_port = 0;
        int64_t child_sock = xscore_socket_accept(sock_id, &r_ip, &r_port);
        if (child_sock < 0) return (uint64_t)child_sock;
        int64_t child_fd = xscore_fd_open_socket(table, (int)child_sock);
        if (child_fd < 0) {
            xscore_socket_close((int)child_sock);
            return (uint64_t)child_fd;
        }
        if (argument1 != 0) {
            if (xscore_socket_domain((int)child_sock) == AKAOS_AF_UNIX) {
                struct akaos_sockaddr_un sun;
                sun.sun_family = AKAOS_AF_UNIX;
                for (int z = 0; z < AKAOS_UNIX_PATH_MAX; ++z) sun.sun_path[z] = 0;
                (void)xscore_copy_to_user(argument1, &sun, sizeof(sun));
            } else {
                struct akaos_sockaddr_in sin;
                sin.sin_family = AKAOS_AF_INET;
                sin.sin_port = (uint16_t)(((r_port >> 8) & 0xff) | ((r_port & 0xff) << 8));
                sin.sin_addr = r_ip;
                for (int z = 0; z < 8; ++z) sin.sin_zero[z] = 0;
                (void)xscore_copy_to_user(argument1, &sin, sizeof(sin));
            }
        }
        return (uint64_t)child_fd;
    }
    if (number == AKAOS_SYSCALL_GETSOCKNAME ||
        number == AKAOS_SYSCALL_GETPEERNAME) {
        if (!table || argument1 == 0U || argument2 == 0U)
            return (uint64_t)-AKAOS_EFAULT;
        if (!xscore_user_range_prepare(argument2, sizeof(uint32_t), 1))
            return (uint64_t)-AKAOS_EFAULT;
        uint32_t user_length = 0U;
        if (!xscore_copy_from_user(&user_length, argument2,
                                   sizeof(user_length)))
            return (uint64_t)-AKAOS_EFAULT;
        int sock_id = xscore_fd_socket_id(table, (int)argument0);
        if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;

        int domain = 0;
        uint32_t ip = 0U;
        uint16_t port = 0U;
        char path[AKAOS_UNIX_PATH_MAX];
        size_t path_length = 0U;
        int peer = number == AKAOS_SYSCALL_GETPEERNAME;
        int64_t result = xscore_socket_get_name(
            sock_id, peer, &domain, &ip, &port, path, sizeof(path),
            &path_length);
        if (result < 0) return (uint64_t)result;

        uint8_t address_buffer[sizeof(struct akaos_sockaddr_un)];
        size_t address_length = 0U;
        memset(address_buffer, 0, sizeof(address_buffer));
        if (domain == AKAOS_AF_UNIX) {
            struct akaos_sockaddr_un address;
            memset(&address, 0, sizeof(address));
            address.sun_family = AKAOS_AF_UNIX;
            size_t copied = path_length < sizeof(address.sun_path) - 1U
                ? path_length : sizeof(address.sun_path) - 1U;
            if (copied) memcpy(address.sun_path, path, copied);
            address.sun_path[copied] = '\0';
            memcpy(address_buffer, &address, sizeof(address));
            address_length = offsetof(struct akaos_sockaddr_un, sun_path) +
                (path_length ? copied + 1U : 0U);
        } else if (domain == AKAOS_AF_INET) {
            struct akaos_sockaddr_in address;
            memset(&address, 0, sizeof(address));
            address.sin_family = AKAOS_AF_INET;
            address.sin_port = (uint16_t)(((port >> 8) & 0xffU) |
                                          ((port & 0xffU) << 8));
            address.sin_addr = ip;
            memcpy(address_buffer, &address, sizeof(address));
            address_length = sizeof(address);
        } else {
            return (uint64_t)-AKAOS_EAFNOSUPPORT;
        }
        size_t amount = user_length < address_length
            ? user_length : address_length;
        if (amount != 0U &&
            !xscore_copy_to_user(argument1, address_buffer, amount))
            return (uint64_t)-AKAOS_EFAULT;
        uint32_t result_length = (uint32_t)address_length;
        if (!xscore_copy_to_user(argument2, &result_length,
                                 sizeof(result_length)))
            return (uint64_t)-AKAOS_EFAULT;
        return 0U;
    }
    if (number == AKAOS_SYSCALL_SENDTO) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        int sock_id = xscore_fd_socket_id(table, (int)argument0);
        if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;
        struct akaos_sock_io_args io_args;
        if (!xscore_copy_from_user(&io_args, argument2, sizeof(io_args))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        if (io_args.len > 1500 || !xscore_user_range_prepare(argument1, io_args.len, 0)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        static uint8_t s_sendto_buf[1536];
        if (!xscore_copy_from_user(s_sendto_buf, argument1, io_args.len)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return (uint64_t)xscore_socket_sendto(sock_id, s_sendto_buf, io_args.len, io_args.ip, io_args.port);
    }
    if (number == AKAOS_SYSCALL_SENDMSG) {
        return (uint64_t)syscall_socket_sendmsg(
            table, argument0, argument1, argument2);
    }
    if (number == AKAOS_SYSCALL_RECVMSG) {
        return (uint64_t)syscall_socket_recvmsg(
            table, argument0, argument1, argument2);
    }
    if (number == AKAOS_SYSCALL_RECVFROM) {
        if (!table) return (uint64_t)-AKAOS_EBADF;
        int sock_id = xscore_fd_socket_id(table, (int)argument0);
        if (sock_id < 0) return (uint64_t)-AKAOS_EBADF;
        struct akaos_sock_io_args io_args;
        if (!xscore_copy_from_user(&io_args, argument2, sizeof(io_args))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        if (io_args.len > 1500 || !xscore_user_range_prepare(argument1, io_args.len, 1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        static uint8_t s_recvfrom_buf[1536];
        int64_t n = xscore_socket_recvfrom(sock_id, s_recvfrom_buf, io_args.len, &io_args.ip, &io_args.port);
        if (n < 0) return (uint64_t)n;
        if (n > 0 && !xscore_copy_to_user(argument1, s_recvfrom_buf, (size_t)n)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        io_args.len = (uint32_t)n;
        (void)xscore_copy_to_user(argument2, &io_args, sizeof(io_args));
        return (uint64_t)n;
    }
    if (number == AKAOS_SYSCALL_MOUNT) {
        struct akaos_mount_args margs;
        if (!xscore_copy_from_user(&margs, argument0, sizeof(margs))) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        char src_buf[64];
        char tgt_buf[128];
        char fst_buf[32];
        src_buf[0] = 0;
        tgt_buf[0] = 0;
        fst_buf[0] = 0;
        if (margs.source) {
            size_t slen = 0;
            while (slen < sizeof(src_buf) - 1) {
                char c = 0;
                if (!xscore_copy_from_user(&c, (uint64_t)(uintptr_t)(margs.source + slen), 1)) break;
                if (!c) break;
                src_buf[slen++] = c;
            }
            src_buf[slen] = 0;
        }
        if (margs.target) {
            size_t tlen = 0;
            while (tlen < sizeof(tgt_buf) - 1) {
                char c = 0;
                if (!xscore_copy_from_user(&c, (uint64_t)(uintptr_t)(margs.target + tlen), 1)) break;
                if (!c) break;
                tgt_buf[tlen++] = c;
            }
            tgt_buf[tlen] = 0;
        } else {
            return (uint64_t)-AKAOS_EFAULT;
        }
        if (margs.fstype) {
            size_t flen = 0;
            while (flen < sizeof(fst_buf) - 1) {
                char c = 0;
                if (!xscore_copy_from_user(&c, (uint64_t)(uintptr_t)(margs.fstype + flen), 1)) break;
                if (!c) break;
                fst_buf[flen++] = c;
            }
            fst_buf[flen] = 0;
        }
        return (uint64_t)xscore_vfs_mount(src_buf[0] ? src_buf : NULL, tgt_buf, fst_buf[0] ? fst_buf : NULL, margs.flags);
    }
    if (number == AKAOS_SYSCALL_UMOUNT) {
        if (!argument0 || argument1 == 0 || argument1 > 127) return (uint64_t)-AKAOS_EINVAL;
        char tgt_buf[128];
        if (!xscore_copy_from_user(tgt_buf, argument0, (size_t)argument1)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        tgt_buf[argument1] = 0;
        return (uint64_t)xscore_vfs_umount(tgt_buf, argument2);
    }
    if (number == AKAOS_SYSCALL_RENAME) {
        char oldpath[AKAOS_SYSCALL_PATH_MAX + 1];
        char newpath[AKAOS_SYSCALL_PATH_MAX + 1];
        if (!xscore_copy_string_from_user(oldpath, sizeof(oldpath), argument0, 0) ||
            !xscore_copy_string_from_user(newpath, sizeof(newpath), argument1, 0)) {
            return (uint64_t)-AKAOS_EFAULT;
        }
        return (uint64_t)(int64_t)xscore_vfs_rename_at(cwd_node, oldpath, cwd_node, newpath);
    }
    if (number == AKAOS_SYSCALL_EXIT) {
        xscore_thread_record_user_exit(argument0);
        return XSCORE_SYSCALL_EXIT_TO_KERNEL;
    }
    return (uint64_t)-AKAOS_ENOSYS;
}

uint64_t xscore_syscall_last_exit_status(void) {
    return xscore_thread_user_exit_status();
}
