#include <xscore/smp.h>
#include <xscore/apic.h>
#include <xscore/acpi.h>
#include <xscore/heap.h>
#include <xscore/memory.h>
#include <xscore/stack.h>
#include <xscore/vmm.h>
#include <xscore/sched.h>
#include <xscore/thread.h>
#include <xscore/process.h>
#include <xscore/timer.h>
#include <xscore/apic.h>
#include <xscore/serial.h>
#include <xscore/file.h>
#include <xscore/vfs.h>
#include <xscore/virtio_blk.h>
#include <xscore/socket.h>
#include <akaos/errno.h>
#include <akaos/poll.h>
#include <stddef.h>
#include <stdint.h>

#define APIC_REG_ID              0x020
#define APIC_REG_TPR             0x080
#define APIC_REG_EOI             0x0B0
#define APIC_REG_SPURIOUS        0x0F0
#define APIC_REG_ICR_LOW         0x300
#define APIC_REG_ICR_HIGH        0x310
#define APIC_SPURIOUS_ENABLE     (1U << 8)
#define MSR_APIC_BASE            0x1B
#define APIC_BASE_ENABLE         (1ULL << 11)

extern volatile uint32_t *apic_base;

extern const uint8_t smp_trampoline_start[];
extern const uint8_t smp_trampoline_end[];
extern volatile uint64_t smp_boot_cr3;
extern volatile uint64_t smp_boot_stack;
extern volatile uint64_t smp_boot_entry;
extern volatile uint32_t smp_boot_status;

extern void xscore_arch_ap_load_tables(void);
extern void xscore_tss_install_core(uint32_t cpu_index, uint64_t ist1, uint64_t ist2, uint64_t rsp0);

static struct xscore_cpu_core cpu_cores[SMP_MAX_CPUS];
static uint32_t online_cpu_count = 1;
static uint32_t bsp_apic_id = 0;
static int smp_initialized = 0;

/* A VMM mutation holds its own global page-table lock.  This second lock
 * serializes the allocation-free IPI request/acknowledgement mailbox so an
 * AP cannot acknowledge one invalidation while the BSP is publishing the
 * next one. */
static xscore_spinlock_t tlb_shootdown_lock = XSCORE_SPINLOCK_INIT;
static volatile uint64_t tlb_shootdown_address;
static volatile uint32_t tlb_shootdown_pending[SMP_MAX_CPUS];
static volatile uint64_t active_address_spaces[SMP_MAX_CPUS];

/* The receiver of a shootdown IPI may itself be waiting for this mailbox
 * lock from an exception path with interrupts masked.  Wait with interrupts
 * enabled while the lock is busy, then disable them for the atomic acquire so
 * the sender can always receive the acknowledgement it is waiting for. */
static uintptr_t tlb_lock_acquire(void) {
    uintptr_t flags = xscore_irq_save();
    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&tlb_shootdown_lock.locked,
                                        &expected, 1, 0, __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED)) {
            return flags;
        }
        __asm__ volatile("sti" ::: "memory");
        while (__atomic_load_n(&tlb_shootdown_lock.locked,
                               __ATOMIC_ACQUIRE)) {
            __asm__ volatile("pause");
        }
        __asm__ volatile("cli" ::: "memory");
    }
}

/* Fixed, allocation-free work slots. AP jobs must not enter the BSP-owned
 * process/VFS/allocator machinery until those subsystems support SMP. */
struct smp_work {
    void (*function)(uint32_t);
    uint32_t pending;
    uint32_t result;
};
static struct smp_work work[SMP_MAX_CPUS];
static xscore_spinlock_t test_lock = XSCORE_SPINLOCK_INIT;
static uint32_t test_counter;
#define SMP_TEST_ITERATIONS 50000U

struct smp_scheduler_job {
    volatile uint32_t result;
    uint32_t expected_cpu;
};

static struct xscore_fd_table smp_test_fd_tables[SMP_MAX_CPUS];
static char smp_test_directories[SMP_MAX_CPUS][32];
static char smp_test_paths[SMP_MAX_CPUS][64];
static char smp_test_moved_paths[SMP_MAX_CPUS][64];
static char smp_test_persist_paths[SMP_MAX_CPUS][64];
static uint8_t smp_test_payloads[SMP_MAX_CPUS][24];
static uint8_t smp_test_readbacks[SMP_MAX_CPUS][24];
static uint8_t smp_test_persist_payloads[SMP_MAX_CPUS][24];

static size_t smp_copy_text(char *buffer, size_t offset, size_t capacity,
                            const char *text) {
    if (!buffer || !text || offset >= capacity) return offset;
    while (*text && offset + 1 < capacity) buffer[offset++] = *text++;
    buffer[offset] = '\0';
    return offset;
}

static size_t smp_append_number(char *buffer, size_t offset, size_t capacity,
                                uint32_t value) {
    char digits[10];
    size_t count = 0;
    if (value == 0) digits[count++] = '0';
    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (count > 0 && offset + 1 < capacity) {
        buffer[offset++] = digits[--count];
    }
    if (offset < capacity) buffer[offset] = '\0';
    return offset;
}

static size_t smp_make_vfs_path(char *buffer, size_t capacity,
                                uint32_t cpu, uint32_t round, int moved) {
    size_t offset = smp_copy_text(buffer, 0, capacity, "/root/xsmp");
    offset = smp_append_number(buffer, offset, capacity, cpu);
    if (offset + 2 < capacity) {
        buffer[offset++] = '/';
        buffer[offset++] = moved ? 'm' : 'f';
        buffer[offset] = '\0';
    }
    return smp_append_number(buffer, offset, capacity, round);
}

static size_t smp_make_persist_path(char *buffer, size_t capacity,
                                    uint32_t cpu) {
    size_t offset = smp_copy_text(buffer, 0, capacity, "/root/xsmp");
    offset = smp_append_number(buffer, offset, capacity, cpu);
    offset = smp_copy_text(buffer, offset, capacity, "/persist");
    return offset;
}

/* Exercise shared VFS, descriptor, character-device, and socket pools from
 * every AP. The first rounds use unique temporary names; when the optional
 * devices are ready, the worker also commits one persistent record and sends
 * a bounded UDP burst through the external VirtIO-Net path. */
static int smp_vfs_device_socket_job(uint32_t cpu) {
    if (cpu >= SMP_MAX_CPUS) return 0;
    char *directory = smp_test_directories[cpu];
    char *path = smp_test_paths[cpu];
    char *moved_path = smp_test_moved_paths[cpu];
    uint8_t *payload = smp_test_payloads[cpu];
    uint8_t *readback = smp_test_readbacks[cpu];
    struct xscore_fd_table *fd_table = &smp_test_fd_tables[cpu];
    size_t offset = smp_copy_text(directory, 0, sizeof(smp_test_directories[cpu]),
                                  "/root/xsmp");
    offset = smp_append_number(directory, offset,
                               sizeof(smp_test_directories[cpu]), cpu);
    (void)offset;

    int mkdir_result = xscore_vfs_mkdir_at(0, directory, 0755);
    if (mkdir_result != 0 && mkdir_result != -AKAOS_EEXIST) return 0;

    for (uint32_t round = 0; round < 8; ++round) {
        smp_make_vfs_path(path, sizeof(smp_test_paths[cpu]), cpu, round, 0);
        smp_make_vfs_path(moved_path, sizeof(smp_test_moved_paths[cpu]), cpu,
                          round, 1);

        int64_t descriptor = xscore_vfs_open_at(
            fd_table, 0, path,
            XSCORE_OPEN_READ_WRITE | XSCORE_OPEN_CREAT | XSCORE_OPEN_TRUNC,
            0644);
        if (descriptor < 0) return 0;

        for (uint32_t index = 0;
             index < sizeof(smp_test_payloads[cpu]); ++index) {
            payload[index] = (uint8_t)(0xA0U ^ cpu ^ round ^ index);
            readback[index] = 0;
        }

        int64_t duplicate = xscore_fd_dup(fd_table, (int)descriptor);
        if (duplicate < 0 ||
            (xscore_fd_poll(fd_table, (int)duplicate,
                            AKAOS_POLLOUT) & AKAOS_POLLOUT) == 0 ||
            xscore_fd_close(fd_table, (int)duplicate) != 0) {
            (void)xscore_fd_close(fd_table, (int)descriptor);
            return 0;
        }

        if (xscore_fd_write(fd_table, (int)descriptor,
                            payload, sizeof(smp_test_payloads[cpu])) !=
                                (int64_t)sizeof(smp_test_payloads[cpu]) ||
            xscore_fd_close(fd_table, (int)descriptor) != 0) {
            return 0;
        }

        descriptor = xscore_vfs_open_at(fd_table, 0, path,
                                        XSCORE_OPEN_READ_ONLY, 0);
        if (descriptor < 0) return 0;
        int64_t bytes_read = xscore_fd_read(fd_table,
                                            (int)descriptor, readback,
                                            sizeof(smp_test_readbacks[cpu]));
        if (bytes_read != (int64_t)sizeof(smp_test_readbacks[cpu])) {
            (void)xscore_fd_close(fd_table, (int)descriptor);
            return 0;
        }
        if (xscore_fd_close(fd_table, (int)descriptor) != 0) {
            return 0;
        }
        for (uint32_t index = 0;
             index < sizeof(smp_test_payloads[cpu]); ++index) {
            if (readback[index] != payload[index]) return 0;
        }

        if (xscore_vfs_rename_at(0, path, 0, moved_path) != 0) return 0;
        struct akaos_stat status;
        if (xscore_vfs_stat_at(0, moved_path, &status) != 0 ||
            status.size != sizeof(smp_test_payloads[cpu])) return 0;
        descriptor = xscore_vfs_open_at(fd_table, 0, moved_path,
                                        XSCORE_OPEN_READ_ONLY, 0);
        if (descriptor < 0 || xscore_fd_close(fd_table,
                                              (int)descriptor) != 0) {
            if (descriptor >= 0) (void)xscore_fd_close(fd_table,
                                                       (int)descriptor);
            return 0;
        }
        if (xscore_vfs_unlink_at(0, moved_path) != 0) return 0;

        int null_fd = (int)xscore_fd_open_null(fd_table, 1, 1);
        if (null_fd < 0 ||
            xscore_fd_write(fd_table, null_fd, payload,
                            sizeof(smp_test_payloads[cpu])) !=
                                (int64_t)sizeof(smp_test_payloads[cpu]) ||
            (xscore_fd_poll(fd_table, null_fd,
                            AKAOS_POLLOUT) & AKAOS_POLLOUT) == 0 ||
            xscore_fd_close(fd_table, null_fd) != 0) {
            if (null_fd >= 0) (void)xscore_fd_close(fd_table, null_fd);
            return 0;
        }

        int zero_fd = (int)xscore_fd_open_zero(fd_table, 1, 0);
        if (zero_fd < 0 ||
            xscore_fd_read(fd_table, zero_fd, readback,
                           sizeof(smp_test_readbacks[cpu])) !=
                                (int64_t)sizeof(smp_test_readbacks[cpu]) ||
            (xscore_fd_poll(fd_table, zero_fd,
                            AKAOS_POLLIN) & AKAOS_POLLIN) == 0 ||
            xscore_fd_close(fd_table, zero_fd) != 0) {
            if (zero_fd >= 0) (void)xscore_fd_close(fd_table, zero_fd);
            return 0;
        }

        int64_t socket_id = xscore_socket_create(AKAOS_AF_INET,
                                                 AKAOS_SOCK_DGRAM,
                                                 AKAOS_IPPROTO_UDP);
        if (socket_id < 0 ||
            xscore_socket_bind((int)socket_id,
                               (127U) | (1U << 24),
                               (uint16_t)(45000U + cpu * 32U + round)) != 0 ||
            !xscore_socket_poll_writable((int)socket_id)) {
            if (socket_id >= 0) xscore_socket_close((int)socket_id);
            return 0;
        }
        int socket_fd = (int)xscore_fd_open_socket(fd_table,
                                                   (int)socket_id);
        if (socket_fd < 0 ||
            (xscore_fd_poll(fd_table, socket_fd,
                            AKAOS_POLLOUT) & AKAOS_POLLOUT) == 0 ||
            xscore_fd_close(fd_table, socket_fd) != 0) {
            if (socket_fd >= 0) (void)xscore_fd_close(fd_table,
                                                      socket_fd);
            xscore_socket_close((int)socket_id);
            return 0;
        }
        xscore_socket_close((int)socket_id);
    }

    /* Keep one uniquely named record on the writable akaFS disk.  Every
     * dynamic write already commits a snapshot; the explicit final sync
     * below verifies that concurrent AP workers can safely commit the final
     * metadata/data state as well.  The record is intentionally retained so
     * the host-side SMP harness can inspect the persisted payload after the
     * guest exits. */
    if (xscore_virtio_blk_present()) {
        char *persist_path = smp_test_persist_paths[cpu];
        uint8_t *persist_payload = smp_test_persist_payloads[cpu];
        smp_make_persist_path(persist_path, sizeof(smp_test_persist_paths[cpu]),
                              cpu);
        for (uint32_t index = 0;
             index < sizeof(smp_test_persist_payloads[cpu]); ++index) {
            persist_payload[index] =
                (uint8_t)('P' ^ cpu ^ (index * 3U));
        }
        int64_t persist_fd = xscore_vfs_open_at(
            fd_table, 0, persist_path,
            XSCORE_OPEN_READ_WRITE | XSCORE_OPEN_CREAT | XSCORE_OPEN_TRUNC,
            0644);
        if (persist_fd < 0 ||
            xscore_fd_write(fd_table, (int)persist_fd, persist_payload,
                            sizeof(smp_test_persist_payloads[cpu])) !=
                (int64_t)sizeof(smp_test_persist_payloads[cpu]) ||
            xscore_fd_close(fd_table, (int)persist_fd) != 0 ||
            !xscore_vfs_persistence_sync()) {
            if (persist_fd >= 0) (void)xscore_fd_close(fd_table,
                                                       (int)persist_fd);
            return 0;
        }
    } else {
        (void)xscore_vfs_unlink_at(0, directory);
    }

    /* Exercise the real VirtIO transmit path when the SMP harness supplies a
     * network device.  Each AP owns a distinct UDP socket and sends a small,
     * bounded burst to QEMU's user-mode gateway; no external response is
     * required, so the test remains deterministic and offline-safe. */
    if (xscore_virtio_net_present()) {
        int64_t external_socket = xscore_socket_create(
            AKAOS_AF_INET, AKAOS_SOCK_DGRAM, AKAOS_IPPROTO_UDP);
        uint16_t port = (uint16_t)(46000U + cpu * 8U);
        uint32_t gateway_ip = 10U | (2U << 16) | (2U << 24);
        if (external_socket < 0 ||
            xscore_socket_bind((int)external_socket, 0, port) != 0) {
            if (external_socket >= 0) xscore_socket_close((int)external_socket);
            return 0;
        }
        for (uint32_t round = 0; round < 4; ++round) {
            for (uint32_t index = 0;
                 index < sizeof(smp_test_payloads[cpu]); ++index) {
                smp_test_payloads[cpu][index] =
                    (uint8_t)(0x60U ^ cpu ^ round ^ index);
            }
            if (xscore_socket_sendto(
                    (int)external_socket, smp_test_payloads[cpu],
                    sizeof(smp_test_payloads[cpu]), gateway_ip,
                    (uint16_t)(9U + round)) !=
                (int64_t)sizeof(smp_test_payloads[cpu])) {
                xscore_socket_close((int)external_socket);
                return 0;
            }
        }
        xscore_socket_close((int)external_socket);
    }
    return 1;
}
static void smp_scheduler_job(void *argument) {
    struct smp_scheduler_job *job = (struct smp_scheduler_job *)argument;
    if (!job) return;
    if (xscore_smp_current_cpu_index() != job->expected_cpu) {
        job->result = 2U;
        return;
    }

    /* Exercise VFS, descriptor/device, socket, and shared allocator state on
     * concurrently scheduled AP workers. */
    if (!smp_vfs_device_socket_job(job->expected_cpu)) {
        job->result = 3U;
        return;
    }
    for (uint32_t round = 0; round < 32; ++round) {
        uint8_t *allocation = xscore_heap_allocate(96U + job->expected_cpu);
        if (!allocation) {
            job->result = 3U;
            return;
        }
        for (uint32_t byte = 0; byte < 96U; ++byte) {
            allocation[byte] = (uint8_t)(byte ^ job->expected_cpu ^ round);
        }
        for (uint32_t byte = 0; byte < 96U; ++byte) {
            if (allocation[byte] !=
                (uint8_t)(byte ^ job->expected_cpu ^ round)) {
                job->result = 3U;
                return;
            }
        }
        if (!xscore_heap_free(allocation)) {
            job->result = 3U;
            return;
        }

        uint64_t page = xscore_pmm_allocate_page();
        if (!page) {
            job->result = 3U;
            return;
        }
        volatile uint64_t *words = (volatile uint64_t *)(uintptr_t)page;
        uint64_t pattern = UINT64_C(0x5853434f52450000) |
                           ((uint64_t)job->expected_cpu << 8) | round;
        words[0] = pattern;
        if (words[0] != pattern || !xscore_pmm_free_page(page)) {
            job->result = 3U;
            return;
        }
    }
    job->result = 1U;
}

static void smp_count_job(uint32_t cpu) {
    for (uint32_t i = 0; i < SMP_TEST_ITERATIONS; ++i) {
        uintptr_t flags = xscore_spin_lock_irqsave(&test_lock);
        ++test_counter;
        xscore_spin_unlock_irqrestore(&test_lock, flags);
    }
    work[cpu].result = xscore_smp_current_cpu_index() == cpu;
}

int xscore_smp_self_test(void) {
    for (uint32_t round = 0; round < 4; ++round) {
        test_counter = 0;
        for (uint32_t cpu = 1; cpu < online_cpu_count; ++cpu) {
            work[cpu].result = 0;
            work[cpu].function = smp_count_job;
            __atomic_store_n(&work[cpu].pending, 1, __ATOMIC_RELEASE);
        }
        smp_count_job(0);
        uint64_t deadline = xscore_timer_ticks() + 10 * xscore_timer_frequency();
        for (uint32_t cpu = 1; cpu < online_cpu_count; ++cpu) {
            while (__atomic_load_n(&work[cpu].pending, __ATOMIC_ACQUIRE)) {
                if (xscore_timer_ticks() >= deadline) return 0;
                __asm__ volatile("pause");
            }
            if (!work[cpu].result) return 0;
        }
        if (test_counter != online_cpu_count * SMP_TEST_ITERATIONS) return 0;
    }
    xscore_serial_write("[PASS] SMP concurrent work and shared-counter stress verified\n");
    return 1;
}

int xscore_smp_scheduler_self_test(void) {
    if (online_cpu_count <= 1) {
        xscore_serial_write("[INFO] SMP scheduler self-test skipped (one CPU online)\n");
        return 1;
    }

    static struct smp_scheduler_job jobs[SMP_MAX_CPUS];
    static uint64_t ids[SMP_MAX_CPUS];
    for (uint32_t cpu = 1; cpu < online_cpu_count; ++cpu) {
        xscore_fd_table_initialize(&smp_test_fd_tables[cpu]);
    }
    for (uint32_t cpu = 1; cpu < online_cpu_count; ++cpu) {
        jobs[cpu].result = 0;
        jobs[cpu].expected_cpu = cpu;
        ids[cpu] = xscore_thread_create_on_cpu(
            smp_scheduler_job, &jobs[cpu], xscore_vmm_root_table(), cpu);
        if (!ids[cpu]) {
            xscore_serial_write("[FAIL] SMP scheduler could not queue AP worker\n");
            return 0;
        }
    }

    uint64_t deadline = xscore_timer_ticks() + 20 * xscore_timer_frequency();
    for (;;) {
        int complete = 1;
        for (uint32_t cpu = 1; cpu < online_cpu_count; ++cpu) {
            if (jobs[cpu].result == 0) complete = 0;
            if (jobs[cpu].result == 2) return 0;
        }
        if (complete || xscore_timer_ticks() >= deadline) break;
        __asm__ volatile("hlt");
    }

    int passed = 1;
    for (uint32_t cpu = 1; cpu < online_cpu_count; ++cpu) {
        if (jobs[cpu].result != 1 || !xscore_thread_reap(ids[cpu])) passed = 0;
    }
    for (uint32_t cpu = 1; cpu < online_cpu_count; ++cpu) {
        xscore_fd_table_close_all(&smp_test_fd_tables[cpu]);
    }
    if (!passed) {
        xscore_serial_write("[FAIL] SMP scheduler or VFS/device/socket stress failed\n");
        return 0;
    }
    xscore_serial_write("[PASS] SMP per-CPU scheduler handoff verified\n");
    xscore_serial_write("[PASS] SMP shared PMM and heap allocator stress verified\n");
    xscore_serial_write("[PASS] SMP concurrent VFS/device/socket stress verified\n");
    if (xscore_virtio_blk_present()) {
        xscore_serial_write("[PASS] SMP persistent VFS transactions verified\n");
    } else {
        xscore_serial_write("[INFO] SMP persistent VFS transaction test skipped (no writable disk)\n");
    }
    if (xscore_virtio_net_present()) {
        xscore_serial_write("[PASS] SMP external network transmit stress verified\n");
    } else {
        xscore_serial_write("[INFO] SMP external network stress skipped (no VirtIO-Net device)\n");
    }
    return 1;
}

int xscore_smp_userspace_self_test(void) {
    if (online_cpu_count <= 1) {
        xscore_serial_write("[INFO] SMP userspace self-test skipped (one CPU online)\n");
        return 1;
    }

    static struct xscore_process *processes[SMP_MAX_CPUS];
    const char *argv[] = {"/bin/hello", "--stack-growth-test", 0};
    for (uint32_t cpu = 1; cpu < online_cpu_count; ++cpu) {
        processes[cpu] = xscore_process_create_path_args_on_cpu_at(
            0, "/bin/hello", 2, argv, cpu);
        if (!processes[cpu]) {
            xscore_serial_write("[FAIL] SMP userspace process could not be created on AP\n");
            for (uint32_t cleanup = 1; cleanup < cpu; ++cleanup) {
                if (processes[cleanup]) {
                    (void)xscore_process_wait(processes[cleanup], 0);
                    (void)xscore_process_reap(processes[cleanup]);
                }
            }
            return 0;
        }
    }

    int passed = 1;
    for (uint32_t cpu = 1; cpu < online_cpu_count; ++cpu) {
        uint64_t status = UINT64_MAX;
        if (!xscore_process_wait(processes[cpu], &status) ||
            !xscore_process_reap(processes[cpu])) {
            passed = 0;
        }
    }
    if (!passed) {
        xscore_serial_write("[FAIL] SMP AP userspace scheduling or reclamation failed\n");
        return 0;
    }
    xscore_serial_write("[PASS] SMP AP userspace scheduling stress verified\n");
    xscore_serial_write("[PASS] SMP AP userspace process reclamation verified\n");
    return 1;
}

static inline void apic_write(uint32_t offset, uint32_t value) {
    apic_base[offset / 4] = value;
}

static inline uint32_t apic_read(uint32_t offset) {
    return apic_base[offset / 4];
}

static int smp_send_fixed_ipi(uint32_t cpu_index, uint8_t vector) {
    if (!smp_initialized || cpu_index >= online_cpu_count || !apic_base ||
        !__atomic_load_n(&cpu_cores[cpu_index].online, __ATOMIC_ACQUIRE)) {
        return 0;
    }
    uint32_t apic_id = cpu_cores[cpu_index].apic_id;
    while (apic_read(APIC_REG_ICR_LOW) & (1U << 12)) __asm__ volatile("pause");
    apic_write(APIC_REG_ICR_HIGH, apic_id << 24);
    apic_write(APIC_REG_ICR_LOW, (uint32_t)vector | (1U << 14));
    return 1;
}

void xscore_smp_request_reschedule(uint32_t cpu_index) {
    if (!smp_initialized || cpu_index >= online_cpu_count ||
        !apic_base || !__atomic_load_n(&cpu_cores[cpu_index].online,
                                       __ATOMIC_ACQUIRE)) return;
    (void)smp_send_fixed_ipi(cpu_index, XSCORE_IRQ_RESCHEDULE);
}

static void tlb_local_invalidate(uint64_t virtual_address) {
    if (virtual_address) {
        __asm__ volatile("invlpg (%0)" : : "r"((void *)(uintptr_t)virtual_address)
                         : "memory");
    }
}

static void tlb_broadcast(uint64_t virtual_address) {
    tlb_local_invalidate(virtual_address);
    if (!smp_initialized) return;

    uintptr_t irq_flags = tlb_lock_acquire();
    uint32_t current = xscore_smp_current_cpu_index();
    __atomic_store_n(&tlb_shootdown_address, virtual_address, __ATOMIC_RELEASE);

    for (uint32_t cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
        uint32_t pending = (cpu < online_cpu_count && cpu != current &&
                            __atomic_load_n(&cpu_cores[cpu].online,
                                            __ATOMIC_ACQUIRE));
        __atomic_store_n(&tlb_shootdown_pending[cpu], pending, __ATOMIC_RELEASE);
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    for (uint32_t cpu = 0; cpu < online_cpu_count; ++cpu) {
        if (!__atomic_load_n(&tlb_shootdown_pending[cpu], __ATOMIC_ACQUIRE)) continue;
        if (!smp_send_fixed_ipi(cpu, XSCORE_IRQ_TLB_SHOOTDOWN)) {
            /* The CPU went offline between the snapshot and the send. */
            __atomic_store_n(&tlb_shootdown_pending[cpu], 0, __ATOMIC_RELEASE);
        }
    }

    for (uint32_t cpu = 0; cpu < online_cpu_count; ++cpu) {
        while (__atomic_load_n(&tlb_shootdown_pending[cpu], __ATOMIC_ACQUIRE)) {
            __asm__ volatile("pause");
        }
    }
    xscore_spin_unlock_irqrestore(&tlb_shootdown_lock, irq_flags);
}

void xscore_smp_note_address_space(uint64_t root_address) {
    uint32_t current = smp_initialized ? xscore_smp_current_cpu_index() : 0;
    if (current < SMP_MAX_CPUS) {
        __atomic_store_n(&active_address_spaces[current], root_address,
                         __ATOMIC_RELEASE);
    }
}

void xscore_smp_tlb_shootdown(uint64_t root_address, uint64_t virtual_address) {
    if (!virtual_address) return;
    /* Kernel mappings are shared by every process root, so they always need a
     * broadcast.  A private process root only needs invalidation on CPUs that
     * have actually loaded that root. */
    if (smp_initialized && root_address != xscore_vmm_root_table()) {
        uintptr_t irq_flags = tlb_lock_acquire();
        uint32_t current = xscore_smp_current_cpu_index();
        tlb_local_invalidate(virtual_address);
        __atomic_store_n(&tlb_shootdown_address, virtual_address,
                         __ATOMIC_RELEASE);
        for (uint32_t cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
            uint32_t pending = (cpu < online_cpu_count && cpu != current &&
                                __atomic_load_n(&cpu_cores[cpu].online,
                                                __ATOMIC_ACQUIRE) &&
                                __atomic_load_n(&active_address_spaces[cpu],
                                                __ATOMIC_ACQUIRE) == root_address);
            __atomic_store_n(&tlb_shootdown_pending[cpu], pending,
                             __ATOMIC_RELEASE);
        }
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        for (uint32_t cpu = 0; cpu < online_cpu_count; ++cpu) {
            if (!__atomic_load_n(&tlb_shootdown_pending[cpu], __ATOMIC_ACQUIRE)) continue;
            if (!smp_send_fixed_ipi(cpu, XSCORE_IRQ_TLB_SHOOTDOWN)) {
                __atomic_store_n(&tlb_shootdown_pending[cpu], 0,
                                 __ATOMIC_RELEASE);
            }
        }
        for (uint32_t cpu = 0; cpu < online_cpu_count; ++cpu) {
            while (__atomic_load_n(&tlb_shootdown_pending[cpu], __ATOMIC_ACQUIRE)) {
                __asm__ volatile("pause");
            }
        }
        xscore_spin_unlock_irqrestore(&tlb_shootdown_lock, irq_flags);
        return;
    }
    tlb_broadcast(virtual_address);
}

void xscore_smp_tlb_shootdown_handler(void) {
    uint64_t virtual_address =
        __atomic_load_n(&tlb_shootdown_address, __ATOMIC_ACQUIRE);
    tlb_local_invalidate(virtual_address);
    uint32_t current = xscore_smp_current_cpu_index();
    if (current < SMP_MAX_CPUS) {
        __atomic_store_n(&tlb_shootdown_pending[current], 0, __ATOMIC_RELEASE);
    }
    xscore_apic_eoi();
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static void memory_copy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
}

static void delay_us(uint32_t us) {
    uint64_t count = (uint64_t)us * 500;
    for (volatile uint64_t i = 0; i < count; ++i) {
        __asm__ volatile("pause");
    }
}

static void serial_print_core(const char *prefix, uint32_t core, const char *mid, uint32_t apic, const char *suffix) {
    xscore_serial_write(prefix);
    char buf[12];
    int idx = 0;
    uint32_t v = core;
    if (v == 0) buf[idx++] = '0';
    else {
        char tmp[10]; int ti = 0;
        while (v > 0) { tmp[ti++] = (char)('0' + (v % 10)); v /= 10; }
        while (ti > 0) buf[idx++] = tmp[--ti];
    }
    buf[idx] = '\0';
    xscore_serial_write(buf);

    xscore_serial_write(mid);
    idx = 0;
    v = apic;
    if (v == 0) buf[idx++] = '0';
    else {
        char tmp[10]; int ti = 0;
        while (v > 0) { tmp[ti++] = (char)('0' + (v % 10)); v /= 10; }
        while (ti > 0) buf[idx++] = tmp[--ti];
    }
    buf[idx] = '\0';
    xscore_serial_write(buf);
    xscore_serial_write(suffix);
}

uint32_t xscore_smp_cpu_count(void) {
    return online_cpu_count;
}

uint32_t xscore_smp_bsp_id(void) {
    return bsp_apic_id;
}

int xscore_smp_is_bsp(void) {
    if (!smp_initialized) return 1;
    return xscore_apic_id() == bsp_apic_id;
}

uint32_t xscore_smp_current_cpu_index(void) {
    if (!smp_initialized) return 0;
    uint32_t apic_id = xscore_apic_id();
    for (uint32_t i = 0; i < SMP_MAX_CPUS; ++i) {
        if (__atomic_load_n(&cpu_cores[i].online, __ATOMIC_ACQUIRE) &&
            cpu_cores[i].apic_id == apic_id) return i;
    }
    return 0;
}

const struct xscore_cpu_core *xscore_smp_get_core(uint32_t cpu_index) {
    if (cpu_index >= online_cpu_count) return NULL;
    return &cpu_cores[cpu_index];
}

void xscore_smp_ap_entry(void) {
    uint64_t apic_msr = rdmsr(MSR_APIC_BASE);
    if (!(apic_msr & APIC_BASE_ENABLE)) {
        apic_msr |= APIC_BASE_ENABLE;
        wrmsr(MSR_APIC_BASE, apic_msr);
    }
    uint32_t my_apic_id = xscore_apic_id();

    uint32_t my_index = 0;
    for (uint32_t i = 1; i < SMP_MAX_CPUS; ++i) {
        if (cpu_cores[i].apic_id == my_apic_id) {
            my_index = i;
            break;
        }
    }

    /* 1. Reload kernel GDT and IDT */
    xscore_arch_ap_load_tables();

    /* 2. Install and load dedicated TSS for this AP */
    xscore_tss_install_core(my_index,
                            cpu_cores[my_index].ist1_top,
                            cpu_cores[my_index].ist2_top,
                            cpu_cores[my_index].syscall_stack_top);

    /* 3. Configure Local APIC on this core */
    apic_write(APIC_REG_SPURIOUS, APIC_SPURIOUS_ENABLE | XSCORE_IRQ_SPURIOUS);
    apic_write(APIC_REG_TPR, 0);

    /* 4. Configure Local APIC timer */
    xscore_timer_ap_initialize();

    /* 5. Register in scheduler */
    xscore_sched_register_core(my_index, my_apic_id, cpu_cores[my_index].kernel_stack_top);

    /* Give this AP an idle TCB. It represents the AP entry loop while the
     * per-CPU run queue is empty and is the safe return point after a worker
     * thread exits. */
    if (!xscore_thread_register_cpu(my_index,
                                    cpu_cores[my_index].syscall_stack_top)) {
        xscore_serial_write("[FATAL] SMP: AP scheduler idle context registration failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    xscore_smp_note_address_space(xscore_vmm_root_table());

    /* 6. Signal online status to BSP */
    size_t off_stat = (uintptr_t)&smp_boot_status - (uintptr_t)smp_trampoline_start;
    volatile uint32_t *trampoline_status = (volatile uint32_t *)(0x8000 + off_stat);
    __atomic_store_n(&cpu_cores[my_index].online, 1, __ATOMIC_RELEASE);
    __atomic_store_n(trampoline_status, 1, __ATOMIC_RELEASE);

    /* 7. Enable interrupts and enter AP idle loop */
    __asm__ volatile("sti");

    for (;;) {
        /* The periodic local timer wakes an idle AP. Disable interrupts across
         * the check and sleep so a wakeup cannot be lost in between. */
        __asm__ volatile("cli" ::: "memory");
        if (xscore_thread_preemption_checkpoint()) {
            continue;
        }
        if (__atomic_load_n(&work[my_index].pending, __ATOMIC_ACQUIRE)) {
            __asm__ volatile("sti" ::: "memory");
            work[my_index].function(my_index);
            __atomic_store_n(&work[my_index].pending, 0, __ATOMIC_RELEASE);
        } else {
            __asm__ volatile("sti; hlt" ::: "memory");
        }
    }
}

static int boot_application_processor(uint32_t cpu_index, uint32_t apic_id) {
    uint64_t stack_top = xscore_stack_allocate(XSCORE_BOOT_STACK_PAGES);
    if (!stack_top) {
        xscore_serial_write("[WARN] SMP: Failed to allocate kernel stack for AP\n");
        return 0;
    }
    uint64_t ist1_top = xscore_stack_allocate(XSCORE_IST_STACK_PAGES);
    uint64_t ist2_top = xscore_stack_allocate(XSCORE_IST_STACK_PAGES);
    uint64_t syscall_stack_top = xscore_stack_allocate(XSCORE_BOOT_STACK_PAGES);
    if (!ist1_top || !ist2_top || !syscall_stack_top) {
        xscore_serial_write("[WARN] SMP: Failed to allocate IST stacks for AP\n");
        return 0;
    }

    cpu_cores[cpu_index].cpu_index = cpu_index;
    cpu_cores[cpu_index].apic_id = apic_id;
    cpu_cores[cpu_index].online = 0;
    cpu_cores[cpu_index].kernel_stack_top = stack_top;
    cpu_cores[cpu_index].ist1_top = ist1_top;
    cpu_cores[cpu_index].ist2_top = ist2_top;
    cpu_cores[cpu_index].syscall_stack_top = syscall_stack_top;
    cpu_cores[cpu_index].tss_selector = (uint16_t)((5 + 2 * cpu_index) * 8);
    cpu_cores[cpu_index].timer_ticks = 0;
    cpu_cores[cpu_index].current_thread = 0;

    size_t off_cr3 = (uintptr_t)&smp_boot_cr3 - (uintptr_t)smp_trampoline_start;
    size_t off_stk = (uintptr_t)&smp_boot_stack - (uintptr_t)smp_trampoline_start;
    size_t off_ent = (uintptr_t)&smp_boot_entry - (uintptr_t)smp_trampoline_start;
    size_t off_stat = (uintptr_t)&smp_boot_status - (uintptr_t)smp_trampoline_start;

    volatile uint64_t *trampoline_cr3 = (volatile uint64_t *)(0x8000 + off_cr3);
    volatile uint64_t *trampoline_stack = (volatile uint64_t *)(0x8000 + off_stk);
    volatile uint64_t *trampoline_entry = (volatile uint64_t *)(0x8000 + off_ent);
    volatile uint32_t *trampoline_status = (volatile uint32_t *)(0x8000 + off_stat);

    *trampoline_cr3 = xscore_vmm_root_table();
    *trampoline_stack = stack_top;
    *trampoline_entry = (uint64_t)(uintptr_t)xscore_smp_ap_entry;
    *trampoline_status = 0;

    __atomic_thread_fence(__ATOMIC_SEQ_CST);

    /* INIT IPI */
    while (apic_read(APIC_REG_ICR_LOW) & (1U << 12)) __asm__ volatile("pause");
    apic_write(APIC_REG_ICR_HIGH, apic_id << 24);
    apic_write(APIC_REG_ICR_LOW, 0x00004500);

    delay_us(10000); /* 10 ms delay */

    /* First Startup IPI (SIPI) with vector 0x08 (0x8000) */
    while (apic_read(APIC_REG_ICR_LOW) & (1U << 12)) __asm__ volatile("pause");
    apic_write(APIC_REG_ICR_HIGH, apic_id << 24);
    apic_write(APIC_REG_ICR_LOW, 0x00004608);

    delay_us(200);

    /* Second Startup IPI if needed */
    if (*trampoline_status == 0) {
        while (apic_read(APIC_REG_ICR_LOW) & (1U << 12)) __asm__ volatile("pause");
        apic_write(APIC_REG_ICR_HIGH, apic_id << 24);
        apic_write(APIC_REG_ICR_LOW, 0x00004608);
    }

    /* Wait for AP to set handshake flag (up to 100 ms) */
    for (int timeout = 0; timeout < 1000; ++timeout) {
        if (__atomic_load_n(trampoline_status, __ATOMIC_ACQUIRE) == 1) {
            return 1;
        }
        delay_us(100);
    }

    xscore_serial_write("[WARN] SMP: Application Processor boot handshake timed out\n");
    return 0;
}

void xscore_smp_initialize(void) {
    bsp_apic_id = xscore_apic_id();

    cpu_cores[0].cpu_index = 0;
    cpu_cores[0].apic_id = bsp_apic_id;
    cpu_cores[0].online = 1;
    cpu_cores[0].kernel_stack_top = xscore_stack_boot_top();
    cpu_cores[0].ist1_top = xscore_stack_ist_top(1);
    cpu_cores[0].ist2_top = xscore_stack_ist_top(2);
    cpu_cores[0].syscall_stack_top = xscore_stack_syscall_top();
    cpu_cores[0].tss_selector = 0x28;
    cpu_cores[0].timer_ticks = 0;
    cpu_cores[0].current_thread = 0;
    active_address_spaces[0] = xscore_vmm_root_table();

    online_cpu_count = 1;
    smp_initialized = 1;

    if (!xscore_acpi_present()) {
        xscore_serial_write("[INFO] SMP: ACPI not present, single-core BSP active\n");
        return;
    }

    const struct xscore_acpi_info *acpi = xscore_acpi_get_info();
    if (!acpi || !acpi->madt_present || acpi->cpu_count <= 1) {
        xscore_serial_write("[INFO] SMP: Single-core topology detected in ACPI MADT\n");
        return;
    }

    serial_print_core("[INFO] SMP: ", acpi->cpu_count, " cores detected in ACPI MADT (BSP APIC ID ", bsp_apic_id, ")\n");

    /* Copy trampoline code to 0x8000 */
    size_t tramp_size = (size_t)(smp_trampoline_end - smp_trampoline_start);
    memory_copy((void *)0x8000, smp_trampoline_start, tramp_size);

    for (uint32_t i = 0; i < acpi->cpu_count && online_cpu_count < SMP_MAX_CPUS; ++i) {
        uint32_t apic_id = acpi->cpu_apic_ids[i];
        if (apic_id == bsp_apic_id) continue;

        uint32_t new_index = online_cpu_count;
        if (boot_application_processor(new_index, apic_id)) {
            online_cpu_count++;
            serial_print_core("[INFO] SMP: Core ", new_index, " (APIC ID ", apic_id, ") online\n");
        } else {
            /* A delayed AP may still be reading the shared trampoline. Never
             * overwrite its parameters or reuse its slot after a timeout. */
            break;
        }
    }

    serial_print_core("[INFO] SMP: All ", online_cpu_count, " cores active (BSP APIC ID ", bsp_apic_id, ")\n");
}
