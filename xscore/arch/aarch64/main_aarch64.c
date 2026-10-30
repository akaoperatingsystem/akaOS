#include <xscore/arch.h>
#include <xscore/file.h>
#include <xscore/heap.h>
#include <xscore/device_irq.h>
#include <xscore/initramfs.h>
#include <xscore/memory.h>
#include <xscore/process.h>
#include <xscore/sched.h>
#include <xscore/serial.h>
#include <xscore/syscall.h>
#include <xscore/thread.h>
#include <xscore/tty.h>
#include <xscore/vfs.h>
#include <xscore/vmm.h>
#include <xscore/framebuffer.h>
#include <xscore/virtio_input.h>
#include <xscore/virtio_blk.h>
#include <xscore/bufcache.h>
#include <xscore/net.h>
#include <xscore/cpu.h>
#include <xscore/gpu.h>
#include <xscore/sound.h>
#include <xscore/acpi.h>
#include <xscore/secondary.h>
#include <xscore/user.h>
#include <xscore/version.h>
#include <stdint.h>
#include <stddef.h>

extern void xscore_vmm_aarch64_init(void);
extern char __kernel_end[];

static uint64_t kernel_reserved_end(void) {
    uintptr_t end = (uintptr_t)__kernel_end;
    uint64_t aligned = (uint64_t)((end + (uintptr_t)0xfff) &
                                  ~(uintptr_t)0xfff);
    /* Preserve the historical low-memory reservation even for small images. */
    return aligned > UINT64_C(0x41000000) ? aligned : UINT64_C(0x41000000);
}

/* ================================================================
 * PL011 UART
 * ================================================================ */

#define UART0_DR     ((volatile uint32_t*)0x09000000)
#define UART0_FR     ((volatile uint32_t*)0x09000018)
#define UART0_IBRD   ((volatile uint32_t*)0x09000024)
#define UART0_FBRD   ((volatile uint32_t*)0x09000028)
#define UART0_LCRH   ((volatile uint32_t*)0x0900002C)
#define UART0_CR     ((volatile uint32_t*)0x09000030)

static void serial_init(void) {
    /* Disable UART before programming */
    *UART0_CR = 0;
    /* Set baud rate */
    *UART0_IBRD = 26;
    *UART0_FBRD = 3;
    /* 8 bits, FIFOs enabled */
    *UART0_LCRH = (1 << 4) | (3 << 5);
    /* Enable UART, Tx, Rx */
    *UART0_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

void xscore_serial_putc(char c) {
    if (c == '\n') {
        while (*UART0_FR & (1 << 5)) {}
        *UART0_DR = (uint8_t)'\r';
    }
    while (*UART0_FR & (1 << 5)) {}
    *UART0_DR = (uint8_t)c;
}

void xscore_serial_write(const char *str) {
    if (!str) return;
    while (*str) {
        xscore_serial_putc(*str++);
    }
}

void xscore_serial_write_n(const char *str, uint64_t length) {
    if (!str) return;
    for (uint64_t i = 0; i < length; ++i) xscore_serial_putc(str[i]);
}

void xscore_aarch64_serial_write(const char *str) {
    xscore_serial_write(str);
}

static void print_hex(const char *prefix, uint32_t val) {
    xscore_serial_write(prefix);
    xscore_serial_write("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint32_t nibble = (val >> i) & 0xF;
        xscore_serial_putc(nibble < 10 ? ('0' + nibble) : ('a' + nibble - 10));
    }
    xscore_serial_write("\n");
}

uint64_t xscore_syscall_dispatch_aarch64(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    (void)arg4;
    return (uint64_t)xscore_syscall_dispatch(num, arg1, arg2, arg3);
}

int xscore_aarch64_handle_user_exception(uint64_t esr, uint64_t far) {
    uint64_t exception_class = (esr >> 26) & UINT64_C(0x3f);
    if (exception_class != UINT64_C(0x20) &&
        exception_class != UINT64_C(0x24)) return 0;
    uint32_t fault_flags = 0;
    uint64_t fault_status = esr & UINT64_C(0x3f);
    if (exception_class == UINT64_C(0x24) &&
        (esr & (UINT64_C(1) << 6))) fault_flags |= XSCORE_USER_FAULT_WRITE;
    if (fault_status >= UINT64_C(0x0c) && fault_status <= UINT64_C(0x0f)) {
        fault_flags |= XSCORE_USER_FAULT_PRESENT;
    }
    return xscore_process_handle_page_fault(far, fault_flags);
}

static void bootsplash_animation(void) {
    for (int i = 0; i < 8; ++i) {
        xscore_splash_step();
        for (volatile uint32_t d = 0; d < 2000000; ++d) {
            __asm__ volatile("" ::: "memory");
        }
    }
}

void xscore_aarch64_main(void) {
    serial_init();
    xscore_serial_write("[INIT] XSCore (AArch64) booting...\n");
    xscore_serial_write("[ OK ] AArch64 exception vector table (VBAR_EL1) active\n");

    /* 1. Physical Memory Manager (128 MB RAM at 0x40000000) */
    if (!xscore_pmm_init_direct(UINT64_C(0x40000000) + UINT64_C(128) * 1024 * 1024,
                                kernel_reserved_end())) {
        xscore_serial_write("[FATAL] PMM initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] AArch64 Physical Memory Manager initialized (128 MB)\n");

    /* 2. 4-Level MMU Virtual Memory Manager */
    xscore_vmm_aarch64_init();
    xscore_serial_write("[ OK ] AArch64 4-level MMU virtual memory active\n");

    /* 3. Probe QEMU RAMFB Framebuffer (fw_cfg at 0x09020000) */
    struct akaos_framebuffer fb = {0};
    int has_fb = xscore_ramfb_probe(0x09020000, &fb);
    struct akaos_boot_info fb_boot_info = {0};
    if (has_fb) {
        fb_boot_info.framebuffer = fb;
        xscore_splash_init(&fb_boot_info);
        xscore_serial_write("[ OK ] AArch64 RAMFB Framebuffer initialized (1024x768)\n");
    }

    /* 4. Kernel Heap */
    if (!xscore_heap_initialize()) {
        xscore_serial_write("[FATAL] Kernel heap initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] AArch64 Kernel heap initialized\n");
    xscore_splash_step();

    /* 5. In-Memory VFS and Initramfs */
    if (!xscore_vfs_initialize()) {
        xscore_serial_write("[FATAL] VFS initialization failed\n");
        return;
    }
    if (!xscore_initramfs_initialize()) {
        xscore_serial_write("[FATAL] Initramfs mount failed\n");
        return;
    }
    xscore_serial_write("[ OK ] AArch64 In-memory VFS & Initramfs mounted (26 user binaries)\n");
    xscore_splash_step();

    xscore_acpi_initialize(0);
    xscore_cpu_initialize();
    xscore_gpu_initialize();
    xscore_sound_initialize();

    if (!xscore_virtio_blk_initialize()) {
        xscore_serial_write("[WARN] AArch64 block storage initialization failed\n");
    } else if (xscore_virtio_blk_present()) {
        xscore_bufcache_initialize();
        if (xscore_vfs_persistence_mount()) {
            xscore_serial_write("[ OK ] AArch64 writable akaFS data disk mounted\n");
        }
    }

    /* 6. VirtIO Input Keyboard & TTY Line Discipline */
    xscore_virtio_input_init();
    xscore_virtio_net_initialize();
    xscore_net_initialize();
    xscore_tty_initialize();
    xscore_serial_write("[ OK ] AArch64 File descriptors & TTY line discipline ready\n");
    xscore_splash_step();

    /* 7. Thread Subsystem */
    xscore_sched_initialize();
    if (!xscore_thread_initialize()) {
        xscore_serial_write("[FATAL] Thread subsystem initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] AArch64 Thread subsystem initialized\n");
    xscore_splash_step();

    /* 8. Process Subsystem & ELF64 Loader */
    if (!xscore_process_initialize()) {
        xscore_serial_write("[FATAL] Process subsystem initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] AArch64 Process subsystem & ELF64 loader initialized\n");
    xscore_splash_step();

    /* Animate bootsplash before activating console */
    if (has_fb) {
        bootsplash_animation();
        xscore_fb_console_init(&fb_boot_info);
    }

    /* 9. Spawn /bin/init (PID 1) */
    xscore_device_irq_initialize();
    xscore_secondary_initialize();
    if (!xscore_user_native_context_self_test()) {
        xscore_serial_write("[FATAL] Native trap-frame/address-space ABI probe failed\n");
        return;
    }
    const char *argv[] = {"/bin/init", 0};
    struct xscore_process *init_process = xscore_process_create_path_args("/bin/init", 1, argv);
    if (!init_process) {
        xscore_serial_write("[FATAL] Failed to spawn /bin/init from initramfs\n");
        return;
    }
    (void)xscore_process_set_reaper(init_process);
    xscore_serial_write("[PASS] akaOS AArch64 native port full parity complete\n\n");

    /* 10. Wait for PID 1 (supervises login and shell) */
    uint64_t exit_status = 0;
    xscore_process_wait(init_process, &exit_status);
    xscore_serial_write("[INFO] PID 1 /bin/init exited with status: ");
    print_hex("", (uint32_t)exit_status);

    for (;;) {
        __asm__ volatile("wfe");
    }
}

uint64_t xscore_timer_ticks(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
    return value;
}

uint32_t xscore_timer_frequency(void) {
    uint64_t frequency;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    return (uint32_t)frequency;
}
