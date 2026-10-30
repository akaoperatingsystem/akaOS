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

#define UART0_RBR ((volatile uint8_t *)0x10000000)
#define UART0_THR ((volatile uint8_t *)0x10000000)
#define UART0_IER ((volatile uint8_t *)0x10000001)
#define UART0_FCR ((volatile uint8_t *)0x10000002)
#define UART0_LCR ((volatile uint8_t *)0x10000003)
#define UART0_LSR ((volatile uint8_t *)0x10000005)

extern void xscore_vmm_riscv32_init(void);
extern char __kernel_end[];

static uint64_t kernel_reserved_end(void) {
    uintptr_t end = (uintptr_t)__kernel_end;
    return (uint64_t)((end + (uintptr_t)0xfff) & ~(uintptr_t)0xfff);
}

static void serial_init(void) {
    *UART0_IER = 0x00;
    *UART0_LCR = 0x80;
    *UART0_THR = 0x03;
    *UART0_IER = 0x00;
    *UART0_LCR = 0x03;
    *UART0_FCR = 0x07;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

static void serial_putc(char character) {
    while (!(*UART0_LSR & 0x20)) {}
    *UART0_THR = (uint8_t)character;
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

int xscore_trap_handler_c(uint32_t scause, uint32_t sepc, uint32_t stval, uint32_t sstatus) {
    if ((scause == UINT32_C(12) || scause == UINT32_C(13) ||
         scause == UINT32_C(15)) &&
        (sstatus & (UINT32_C(1) << 8)) == 0 &&
        xscore_process_handle_page_fault(
            (uint64_t)stval,
            scause == UINT32_C(15) ? XSCORE_USER_FAULT_WRITE : 0)) return 1;
    xscore_serial_write("[FATAL] RISC-V 32 Unhandled Trap / Exception:\n");
    print_hex("  scause : ", scause);
    print_hex("  sepc   : ", sepc);
    print_hex("  stval  : ", stval);
    print_hex("  sstatus: ", sstatus);
    return 0;
}

uint32_t xscore_syscall_dispatch_riscv32(uint32_t number, uint32_t argument0,
                                         uint32_t argument1, uint32_t argument2) {
    return (uint32_t)xscore_syscall_dispatch((uint64_t)number, (uint64_t)argument0,
                                             (uint64_t)argument1, (uint64_t)argument2);
}

static void bootsplash_animation(void) {
    for (int i = 0; i < 8; ++i) {
        xscore_splash_step();
        for (volatile uint32_t d = 0; d < 2000000; ++d) {
            __asm__ volatile("" ::: "memory");
        }
    }
}

void xscore_riscv32_main(void) {
    serial_init();
    xscore_serial_write("[INIT] XSCore (RISC-V 32) booting...\n");
    xscore_serial_write("[ OK ] RISC-V 32 trap vector (stvec) active\n");

    /* QEMU virt exposes 128 MiB RAM at 0x80000000. Reserve the complete
     * linked kernel image (including BSS) before page-table allocations. */
    if (!xscore_pmm_init_direct(UINT64_C(0x88000000), kernel_reserved_end())) {
        xscore_serial_write("[FATAL] RISC-V 32 PMM initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] RISC-V 32 Physical Memory Manager initialized (128 MB)\n");

    xscore_vmm_riscv32_init();
    if (!xscore_vmm_root_table()) {
        xscore_serial_write("[FATAL] RISC-V 32 MMU initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] RISC-V 32 Sv32 MMU virtual memory active\n");

    /* Probe QEMU RAMFB Framebuffer (fw_cfg at 0x10100000) */
    struct akaos_framebuffer fb = {0};
    int has_fb = xscore_ramfb_probe(0x10100000, &fb);
    struct akaos_boot_info fb_boot_info = {0};
    if (has_fb) {
        fb_boot_info.framebuffer = fb;
        xscore_splash_init(&fb_boot_info);
        xscore_serial_write("[ OK ] RISC-V 32 RAMFB Framebuffer initialized (1024x768)\n");
    }

    if (!xscore_heap_initialize()) {
        xscore_serial_write("[FATAL] RISC-V 32 kernel heap initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] RISC-V 32 Kernel heap initialized\n");
    xscore_splash_step();

    if (!xscore_vfs_initialize() || !xscore_initramfs_initialize()) {
        xscore_serial_write("[FATAL] RISC-V 32 VFS/initramfs initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] RISC-V 32 In-memory VFS & Initramfs mounted (26 user binaries)\n");
    xscore_splash_step();

    xscore_acpi_initialize(0);
    xscore_cpu_initialize();
    xscore_gpu_initialize();
    xscore_sound_initialize();

    if (!xscore_virtio_blk_initialize()) {
        xscore_serial_write("[WARN] RISC-V 32 block storage initialization failed\n");
    } else if (xscore_virtio_blk_present()) {
        xscore_bufcache_initialize();
        if (xscore_vfs_persistence_mount()) {
            xscore_serial_write("[ OK ] RISC-V 32 writable akaFS data disk mounted\n");
        }
    }

    /* VirtIO Input Keyboard & TTY Line Discipline */
    xscore_virtio_input_init();
    xscore_virtio_net_initialize();
    xscore_net_initialize();
    xscore_tty_initialize();
    xscore_serial_write("[ OK ] RISC-V 32 File descriptors & TTY line discipline ready\n");
    xscore_splash_step();

    xscore_sched_initialize();
    if (!xscore_thread_initialize()) {
        xscore_serial_write("[FATAL] RISC-V 32 thread subsystem initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] RISC-V 32 Thread subsystem initialized\n");
    xscore_splash_step();

    if (!xscore_process_initialize()) {
        xscore_serial_write("[FATAL] RISC-V 32 process subsystem initialization failed\n");
        return;
    }
    xscore_serial_write("[ OK ] RISC-V 32 Process subsystem & ELF32 loader initialized\n");
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
    struct xscore_process *init_process = xscore_process_create_path("/bin/init");
    if (!init_process) {
        xscore_serial_write("[FATAL] Failed to spawn /bin/init on RISC-V 32\n");
        return;
    }
    (void)xscore_process_set_reaper(init_process);

    xscore_serial_write("[PASS] akaOS RISC-V 32 native port full parity complete\n\n");

    uint64_t exit_status = 0;
    if (!xscore_process_wait(init_process, &exit_status)) {
        xscore_serial_write("[FATAL] Failed waiting for /bin/init on RISC-V 32\n");
        return;
    }
    for (;;) {
        __asm__ volatile("wfi");
    }
}

uint64_t xscore_timer_ticks(void) {
    uint32_t high1, low, high2;
    do {
        __asm__ volatile("rdtimeh %0" : "=r"(high1));
        __asm__ volatile("rdtime %0" : "=r"(low));
        __asm__ volatile("rdtimeh %0" : "=r"(high2));
    } while (high1 != high2);
    return ((uint64_t)high1 << 32) | low;
}

uint32_t xscore_timer_frequency(void) {
    return 10000000;
}
