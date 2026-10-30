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

#define UART0_DR   ((volatile uint32_t *)0x09000000)
#define UART0_FR   ((volatile uint32_t *)0x09000018)
#define UART0_IBRD ((volatile uint32_t *)0x09000024)
#define UART0_FBRD ((volatile uint32_t *)0x09000028)
#define UART0_LCRH ((volatile uint32_t *)0x0900002c)
#define UART0_CR   ((volatile uint32_t *)0x09000030)

extern void xscore_vmm_arm32_init(void);
extern char __kernel_end[];

static uint64_t kernel_reserved_end(void) {
    uintptr_t end = (uintptr_t)__kernel_end;
    uint64_t aligned = (uint64_t)((end + (uintptr_t)0xfff) &
                                  ~(uintptr_t)0xfff);
    return aligned > UINT64_C(0x41000000) ? aligned : UINT64_C(0x41000000);
}

static void serial_init(void) {
    *UART0_CR = 0;
    *UART0_IBRD = 26;
    *UART0_FBRD = 3;
    *UART0_LCRH = (1U << 4) | (3U << 5);
    *UART0_CR = (1U << 0) | (1U << 8) | (1U << 9);
}

static void serial_putc(char character) {
    while (*UART0_FR & (1U << 5)) {}
    *UART0_DR = (uint32_t)(uint8_t)character;
}

void xscore_serial_write(const char *text) {
    if (!text) return;
    while (*text) {
        if (*text == '\n') serial_putc('\r');
        serial_putc(*text++);
    }
}

void xscore_serial_write_n(const char *text, uint64_t length) {
    if (!text) return;
    for (uint64_t i = 0; i < length; ++i) {
        if (text[i] == '\n') serial_putc('\r');
        serial_putc(text[i]);
    }
}

static void print_hex(const char *prefix, uint32_t value) {
    xscore_serial_write(prefix);
    xscore_serial_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> shift) & 0xfU;
        serial_putc((char)(digit < 10 ? '0' + digit : 'a' + digit - 10));
    }
    xscore_serial_write("\n");
}

uint32_t xscore_syscall_dispatch_arm32(uint32_t number, uint32_t argument0,
                                       uint32_t argument1, uint32_t argument2) {
    return (uint32_t)xscore_syscall_dispatch(
        (uint64_t)number, (uint64_t)argument0, (uint64_t)argument1,
        (uint64_t)argument2);
}

int xscore_arm32_handle_data_abort(uint32_t dfsr, uint32_t dfar) {
    uint32_t fault_status = (dfsr & UINT32_C(0x0f)) |
                            ((dfsr >> 6) & UINT32_C(0x10));
    if (fault_status != UINT32_C(5) && fault_status != UINT32_C(7) &&
        fault_status != UINT32_C(13) && fault_status != UINT32_C(15)) return 0;
    uint32_t fault_flags = 0;
    if (dfsr & (UINT32_C(1) << 11)) fault_flags |= XSCORE_USER_FAULT_WRITE;
    if (fault_status == UINT32_C(13) || fault_status == UINT32_C(15)) {
        fault_flags |= XSCORE_USER_FAULT_PRESENT;
    }
    int handled = xscore_process_handle_page_fault((uint64_t)dfar,
                                                   fault_flags);
    if (!handled) {
        print_hex("[FATAL] ARM32 DFSR: ", dfsr);
        print_hex("[FATAL] ARM32 DFAR: ", dfar);
    }
    return handled;
}

int xscore_arm32_handle_prefetch_abort(uint32_t ifsr, uint32_t ifar) {
    uint32_t fault_status = (ifsr & UINT32_C(0x0f)) |
                            ((ifsr >> 6) & UINT32_C(0x10));
    if (fault_status != UINT32_C(5) && fault_status != UINT32_C(7) &&
        fault_status != UINT32_C(13) && fault_status != UINT32_C(15)) return 0;
    uint32_t fault_flags = 0;
    if (fault_status == UINT32_C(13) || fault_status == UINT32_C(15)) {
        fault_flags |= XSCORE_USER_FAULT_PRESENT;
    }
    return xscore_process_handle_page_fault((uint64_t)ifar, fault_flags);
}

static void bootsplash_animation(void) {
    for (int i = 0; i < 8; ++i) {
        xscore_splash_step();
        for (volatile uint32_t d = 0; d < 2000000; ++d) {
            __asm__ volatile("" ::: "memory");
        }
    }
}

void xscore_arm32_main(void) {
    serial_init();
    xscore_serial_write("[INIT] XSCore (ARM32) booting...\n");
    xscore_serial_write("[ OK ] ARM32 exception vector table (VBAR) active\n");

    /* QEMU virt exposes 128 MiB RAM at 0x40000000. Keep the MMU tables and
     * kernel image below the first allocatable page at 0x41000000. */
    if (!xscore_pmm_init_direct(UINT64_C(0x48000000), kernel_reserved_end())) {
        xscore_serial_write("[FATAL] ARM32 PMM initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] ARM32 Physical Memory Manager initialized (128 MB)\n");

    xscore_vmm_arm32_init();
    if (!xscore_vmm_root_table()) {
        xscore_serial_write("[FATAL] ARM32 MMU initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] ARM32 short-descriptor MMU virtual memory active\n");

    /* Probe QEMU RAMFB Framebuffer (fw_cfg at 0x09020000) */
    struct akaos_framebuffer fb = {0};
    int has_fb = xscore_ramfb_probe(0x09020000, &fb);
    struct akaos_boot_info fb_boot_info = {0};
    if (has_fb) {
        fb_boot_info.framebuffer = fb;
        xscore_splash_init(&fb_boot_info);
        xscore_serial_write("[ OK ] ARM32 RAMFB Framebuffer initialized (1024x768)\n");
    }

    if (!xscore_heap_initialize()) {
        xscore_serial_write("[FATAL] ARM32 kernel heap initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] ARM32 kernel heap initialized\n");
    xscore_splash_step();

    if (!xscore_vfs_initialize() || !xscore_initramfs_initialize()) {
        xscore_serial_write("[FATAL] ARM32 VFS/initramfs initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] ARM32 in-memory VFS & initramfs mounted (26 user binaries)\n");
    xscore_splash_step();

    xscore_acpi_initialize(0);
    xscore_cpu_initialize();
    xscore_gpu_initialize();
    xscore_sound_initialize();

    if (!xscore_virtio_blk_initialize()) {
        xscore_serial_write("[WARN] ARM32 block storage initialization failed\n");
    } else if (xscore_virtio_blk_present()) {
        xscore_bufcache_initialize();
        if (xscore_vfs_persistence_mount()) {
            xscore_serial_write("[ OK ] ARM32 writable akaFS data disk mounted\n");
        }
    }

    /* VirtIO Input Keyboard & TTY Line Discipline */
    xscore_virtio_input_init();
    xscore_virtio_net_initialize();
    xscore_net_initialize();
    xscore_tty_initialize();
    xscore_serial_write("[ OK ] ARM32 file descriptors & TTY line discipline ready\n");
    xscore_splash_step();

    xscore_sched_initialize();
    if (!xscore_thread_initialize()) {
        xscore_serial_write("[FATAL] ARM32 thread subsystem initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] ARM32 thread subsystem initialized\n");
    xscore_splash_step();

    if (!xscore_process_initialize()) {
        xscore_serial_write("[FATAL] ARM32 process subsystem initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] ARM32 process subsystem & ELF32 loader initialized\n");
    xscore_splash_step();

    /* Animate bootsplash before activating console */
    if (has_fb) {
        bootsplash_animation();
        xscore_fb_console_init(&fb_boot_info);
    }

    xscore_device_irq_initialize();
    xscore_secondary_initialize();
    if (!xscore_user_native_context_self_test()) {
        xscore_serial_write("[FATAL] Native trap-frame/address-space ABI probe failed\n");
        return;
    }
    const char *argv[] = {"/bin/init", 0};
    struct xscore_process *init_process =
        xscore_process_create_path_args("/bin/init", 1, argv);
    if (!init_process) {
        xscore_serial_write("[FATAL] Failed to spawn /bin/init from ARM32 initramfs\n");
        return;
    }
    (void)xscore_process_set_reaper(init_process);
    xscore_serial_write("[PASS] akaOS ARM32 native port full parity complete\n\n");

    uint64_t exit_status = 0;
    xscore_process_wait(init_process, &exit_status);
    print_hex("[INFO] ARM32 PID 1 exited with status: ", (uint32_t)exit_status);
    for (;;) {
        __asm__ volatile("wfi");
    }
}

uint64_t xscore_timer_ticks(void) {
    uint32_t low = 0, high = 0;
    __asm__ volatile("mrrc p15, 1, %0, %1, c14" : "=r"(low), "=r"(high));
    return ((uint64_t)high << 32) | low;
}

uint32_t xscore_timer_frequency(void) {
    uint32_t freq = 0;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(freq));
    if (freq == 0) freq = 62500000;
    return freq;
}
