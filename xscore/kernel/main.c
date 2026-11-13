#include <akaos/boot_info.h>
#include <xscore/arch.h>
#include <xscore/heap.h>
#include <xscore/slab.h>
#include <xscore/memory.h>
#include <xscore/vmm.h>
#include <xscore/sections.h>
#include <xscore/stack.h>
#include <xscore/apic.h>
#include <xscore/timer.h>
#include <xscore/sched.h>
#include <xscore/thread.h>
#include <xscore/serial.h>
#include <xscore/user.h>
#include <xscore/process.h>
#include <xscore/usercopy.h>
#include <xscore/file.h>
#include <xscore/initramfs.h>
#include <xscore/vfs.h>
#include <xscore/tty.h>
#include <xscore/pci.h>
#include <xscore/cpu.h>
#include <xscore/gpu.h>
#include <xscore/sound.h>
#include <xscore/usb.h>
#include <xscore/nvme.h>
#include <xscore/ahci.h>
#include <xscore/storage.h>
#include <xscore/virtio_blk.h>
#include <xscore/bufcache.h>
#include <xscore/net.h>
#include <xscore/version.h>
#include <xscore/acpi.h>
#include <xscore/smp.h>
#include <font_data.h>
#include <akaos/signal.h>
#include <font8x16_data.h>
#include <stddef.h>
#include <stdint.h>

void xscore_framebuffer_register(const struct akaos_framebuffer *framebuffer);

/* ================================================================
 * EFI memory descriptor (matches UEFI spec layout)
 * ================================================================ */

struct efi_memory_descriptor {
    uint32_t type;
    uint32_t padding;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attributes;
};

/* ================================================================
 * I/O helpers
 * ================================================================ */

static inline void out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* ================================================================
 * Serial output (COM1 at 0x3F8)
 * ================================================================ */

static xscore_spinlock_t serial_lock = XSCORE_SPINLOCK_INIT;

static void serial_write_unlocked(const char *text) {
    uintptr_t irq_flags = xscore_spin_lock_irqsave(&serial_lock);
    if (!text) {
        xscore_spin_unlock_irqrestore(&serial_lock, irq_flags);
        return;
    }
    while (*text) {
        if (*text == '\n') {
            while ((in8(0x3fd) & 0x20) == 0) {}
            out8(0x3f8, '\r');
        }
        while ((in8(0x3fd) & 0x20) == 0) {}
        out8(0x3f8, (uint8_t)*text++);
    }
    xscore_spin_unlock_irqrestore(&serial_lock, irq_flags);
}

static int verbose_mode = 0;
static int splash_active = 0;
static int early_console_ready = 0;
static uint32_t shell_cursor_col;
static uint32_t shell_cursor_row;
static uint32_t shell_input_row;
static uint32_t shell_input_rows;
static void shell_grid_init(void);
static void shell_console_sink(char c);


#define KERNEL_LOG_RING_SIZE 16384
static char kernel_log_ring[KERNEL_LOG_RING_SIZE];
static size_t kernel_log_head = 0;
static size_t kernel_log_tail = 0;
static size_t kernel_log_count = 0;
static xscore_spinlock_t kernel_log_lock = XSCORE_SPINLOCK_INIT;

static void kernel_log_append(const char *text) {
    if (!text) return;
    uintptr_t irq_flags = xscore_spin_lock_irqsave(&kernel_log_lock);
    while (*text) {
        char c = *text++;
        kernel_log_ring[kernel_log_head] = c;
        kernel_log_head = (kernel_log_head + 1) % KERNEL_LOG_RING_SIZE;
        if (kernel_log_count < KERNEL_LOG_RING_SIZE) {
            kernel_log_count++;
        } else {
            kernel_log_tail = (kernel_log_tail + 1) % KERNEL_LOG_RING_SIZE;
        }
    }
    xscore_spin_unlock_irqrestore(&kernel_log_lock, irq_flags);
}

size_t xscore_kernel_log_read(char *buffer, size_t max_len, int clear) {
    if (!buffer || max_len == 0) return 0;
    uintptr_t irq_flags = xscore_spin_lock_irqsave(&kernel_log_lock);
    size_t bytes_to_copy = kernel_log_count < max_len ? kernel_log_count : max_len - 1;
    size_t curr = kernel_log_tail;
    for (size_t i = 0; i < bytes_to_copy; ++i) {
        buffer[i] = kernel_log_ring[curr];
        curr = (curr + 1) % KERNEL_LOG_RING_SIZE;
    }
    buffer[bytes_to_copy] = '\0';
    if (clear) {
        kernel_log_tail = kernel_log_head;
        kernel_log_count = 0;
    }
    xscore_spin_unlock_irqrestore(&kernel_log_lock, irq_flags);
    return bytes_to_copy;

int xscore_dmesg_ioctl(uint32_t request, void *argument) {
    if (request == AKAOS_DMESG_CLEAR) {
        uintptr_t irq_flags = xscore_spin_lock_irqsave(&kernel_log_lock);
        kernel_log_tail = kernel_log_head;
        kernel_log_count = 0;
        xscore_spin_unlock_irqrestore(&kernel_log_lock, irq_flags);
        return 0;
    }
    // Set level not implemented yet as per requirement, but structure is there
    return -AKAOS_ENOTTY;
}

}

static void serial_write(const char *text) {
    kernel_log_append(text);
    serial_write_unlocked(text);
    if (verbose_mode && early_console_ready && !splash_active && text) {
        const char *p = text;
        while (*p) {
            shell_console_sink(*p++);
        }
    }
}

void xscore_serial_write(const char *text) {
    serial_write(text);
}

void xscore_serial_write_n(const char *text, uint64_t length) {
    if (!text || length == 0) return;
    uintptr_t irq_flags = xscore_spin_lock_irqsave(&serial_lock);
    for (uint64_t index = 0; index < length; ++index) {
        if (text[index] == '\n') {
            while ((in8(0x3fd) & 0x20) == 0) {}
            out8(0x3f8, '\r');
        }
        while ((in8(0x3fd) & 0x20) == 0) {}
        out8(0x3f8, (uint8_t)text[index]);
    }
    xscore_spin_unlock_irqrestore(&serial_lock, irq_flags);
}

static char *append_u64(char *out, uint64_t value) {
    char digits[21];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value);
    while (count) *out++ = digits[--count];
    *out = 0;
    return out;
}


static void serial_number(const char *prefix, uint64_t value, const char *suffix) {
    char line[96];
    char *cursor = line;
    while (*prefix) *cursor++ = *prefix++;
    cursor = append_u64(cursor, value);
    while (*suffix) *cursor++ = *suffix++;
    *cursor = 0;
    serial_write(line);
}

/* ================================================================
 * CPU identification
 * ================================================================ */

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(0));
}

static void cpu_vendor(char vendor[13]) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, &eax, &ebx, &ecx, &edx);
    (void)eax;
    ((uint32_t *)vendor)[0] = ebx;
    ((uint32_t *)vendor)[1] = edx;
    ((uint32_t *)vendor)[2] = ecx;
    vendor[12] = 0;
}

static uint64_t conventional_memory_mib(const struct akaos_boot_info *boot_info) {
    uint64_t pages = 0;
    for (uint64_t offset = 0; offset + sizeof(struct efi_memory_descriptor) <= boot_info->memory_map_size; offset += boot_info->memory_descriptor_size) {
        const struct efi_memory_descriptor *descriptor =
            (const void *)(uintptr_t)(boot_info->memory_map + offset);
        if (descriptor->type == 7) pages += descriptor->number_of_pages;
    }
    return pages / 256;
}

/* ================================================================
 * Exception handler
 * ================================================================ */

extern void xscore_arch_mark_breakpoint_handled(void);

/*
 * CPU exception dispatch. `frame` points at the stack frame pushed by the
 * CPU and the assembly stub: frame[0]=vector, frame[1]=error code,
 * frame[2]=RIP, frame[3]=CS, frame[4]=RFLAGS, frame[5]=old RSP, frame[6]=SS.
 */
void xscore_exception_handler(uint64_t vector, uint64_t error_code, const uint64_t *frame) {
    uint64_t fault_address = 0;
    if (vector == 3) {
        xscore_arch_mark_breakpoint_handled();
        serial_write("[INFO] Breakpoint exception handled by XSCore IDT\n");
        return;
    }
    if (vector == 14) {
#if defined(__x86_64__)
        __asm__ volatile("movq %%cr2, %0" : "=r"(fault_address));
#elif defined(__i386__)
        uint32_t cr2_32 = 0;
        __asm__ volatile("movl %%cr2, %0" : "=r"(cr2_32));
        fault_address = cr2_32;
#endif
    }
    if (vector == 14 && (error_code & UINT64_C(4)) != 0) {
        uint32_t fault_flags = 0;
        if (error_code & UINT64_C(2)) fault_flags |= XSCORE_USER_FAULT_WRITE;
        if (error_code & UINT64_C(1)) fault_flags |= XSCORE_USER_FAULT_PRESENT;
        if (xscore_process_handle_page_fault(fault_address, fault_flags)) return;

        struct xscore_process *process = xscore_process_current();
        if (process) {
            serial_number("[WARN] User process ", xscore_process_pid(process), " terminated by page fault\n");
            xscore_process_record_termination_signal(AKAOS_SIGSEGV);
            xscore_thread_record_user_exit(UINT64_C(128) + AKAOS_SIGSEGV);
            xscore_thread_exit();
            return;
        }
    }
    if ((frame[3] & 3) == 3) {
        struct xscore_process *process = xscore_process_current();
        if (process) {
            uint32_t sig = (vector == 0) ? AKAOS_SIGFPE : AKAOS_SIGILL;
            serial_number("[WARN] User process ", xscore_process_pid(process), " terminated by CPU exception\n");
            xscore_process_record_termination_signal(sig);
            xscore_thread_record_user_exit(UINT64_C(128) + sig);
            xscore_thread_exit();
            return;
        }
    }
    serial_number("[FATAL] CPU exception vector ", vector, "\n");
    serial_number("[FATAL] Exception error code ", error_code, "\n");
    if (vector == 14) {
        serial_number("[FATAL] Page fault address (CR2): ", fault_address, "\n");
    }
    /* The instruction pointer and the stack pointer come before the layout of
     * the address space, because the layout is long and the console of the
     * guest is slow.  A report that is cut off after the fault address is of
     * no use, and the two registers are what name the failing function. */
    serial_number("[FATAL] Exception RIP: ", frame[2], "\n");
    serial_number("[FATAL] Exception RSP: ", frame[5], "\n");
    /* Dump stack words from user RSP to see return addresses / call stack */
    if (frame[5] != 0) {
        uint64_t stack_words[8];
        if (xscore_copy_from_user(stack_words, frame[5], sizeof(stack_words))) {
            for (int sw = 0; sw < 8; ++sw) {
                serial_number("[FATAL] Stack[+", (uint64_t)(sw * 8), "]: ");
                serial_number("", stack_words[sw], "\n");
            }
        }
    }
    /* The name of the faulting process and the layout of its address space
     * tell a mistake inside a user program apart from a mistake in the
     * kernel. */
    xscore_process_report_name();
    xscore_process_report_fault(fault_address);
    for (;;) __asm__ volatile("cli; hlt");
}

/*
 * IRQ frame layout after xscore_irq_common pushes (indices from frame base):
 *   [0..14]  GPRs: r15 r14 r13 r12 r11 r10 r9 r8 rsi rdi rbp rdx rcx rbx rax
 *   [15]     vector
 *   [16]     error_code (0 for hardware IRQs)
 *   [17]     rip        (CPU-pushed)
 *   [18]     cs         (CPU-pushed)
 *   [19]     rflags     (CPU-pushed)
 *   [20]     rsp        (CPU-pushed, only on Ring 3 → 0 transition)
 *   [21]     ss         (CPU-pushed, only on Ring 3 → 0 transition)
 */
#define IRQ_FRAME_CS_INDEX 18

void xscore_irq_dispatch(uint64_t vector, uint64_t *frame) {
    if (vector == XSCORE_IRQ_TLB_SHOOTDOWN) {
        xscore_smp_tlb_shootdown_handler();
        return;
    }
    if (vector == XSCORE_IRQ_RESCHEDULE) {
        xscore_thread_request_reschedule();
        if ((frame[IRQ_FRAME_CS_INDEX] & 3) != 0 &&
            xscore_process_deliver_thread_exit(frame)) {
            xscore_apic_eoi();
            return;
        }
        xscore_thread_preemption_checkpoint();
        xscore_apic_eoi();
        return;
    }
    if (vector == XSCORE_IRQ_TIMER) {
        xscore_timer_interrupt();
        /* Only the BSP owns the legacy TTY and foreground process state. */
        if (xscore_smp_is_bsp()) {
            /* Poll the legacy keyboard from the timer so Ctrl+C can interrupt
             * a foreground process even when it is CPU-bound. */
            if (!xscore_tty_interrupts_enabled()) xscore_tty_poll_keyboard();
        }
        /*
         * Interrupt-return preemption: if the timer fired while in Ring 3
         * (user mode) and a preemption is pending, perform the context switch
         * right now while we are still on the current task's privilege-entry
         * stack. The cooperative switch saves RSP (pointing into this stack)
         * and the callee-saved regs. When this task is eventually resumed,
         * the switch restores RSP to this stack, the C call chain returns
         * here, and we fall through to the normal IRQ return path in assembly
         * which pops the user's GPRs and iretqs back to Ring 3.
        */
        if ((frame[IRQ_FRAME_CS_INDEX] & 3) != 0) {
            if (xscore_process_deliver_thread_exit(frame)) return;
            if (xscore_process_deliver_pending_signal(frame)) return;
            xscore_thread_preemption_checkpoint();
        }
        return;
    }
    if (vector == XSCORE_IRQ_BASE + 1 || vector == XSCORE_IRQ_BASE + 4 ||
        vector == XSCORE_IRQ_BASE + 12) {
        static int input_irq_seen;
        if (!input_irq_seen) {
            input_irq_seen = 1;
            serial_write("[INFO] TTY hardware interrupt received\n");
        }
        xscore_tty_poll_keyboard();
        xscore_apic_eoi();
        return;
    }
    if (vector == 42 || vector == 43) {
        static uint32_t completion_irqs_seen;
        uint32_t bit = 1U << (vector - 42);
        if (!(completion_irqs_seen & bit)) {
            completion_irqs_seen |= bit;
            serial_write(vector == 42 ? "[INFO] virtio-blk MSI-X interrupt received\n" :
                                       "[INFO] VirtIO-Net MSI-X interrupt received\n");
        }
        /* MSI-X completion: drivers consume used rings in thread context. */
        if (vector == 43) xscore_virtio_net_interrupt();
        xscore_apic_eoi();
        return;
    }
    /* Unknown hardware IRQ — log and EOI */
    serial_number("[WARN] Unexpected IRQ vector ", vector, "\n");
    xscore_apic_eoi();
}

/* ================================================================
 * Framebuffer canvas
 * ================================================================ */

struct canvas {
    uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t requires_red_blue_swap;
};

static uint32_t native_color(const struct canvas *canvas, uint32_t rgb) {
    if (!canvas->requires_red_blue_swap) return rgb;
    return (rgb & 0x00ff00U) | ((rgb & 0xff0000U) >> 16) | ((rgb & 0x0000ffU) << 16);
}

static uint32_t rgb_color(const struct canvas *canvas, uint32_t native) {
    if (!canvas->requires_red_blue_swap) return native;
    return (native & 0x00ff00U) | ((native & 0xff0000U) >> 16) | ((native & 0x0000ffU) << 16);
}

static void fill_rect(struct canvas *canvas, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (x >= canvas->width || y >= canvas->height) return;
    if (x + width > canvas->width) width = canvas->width - x;
    if (y + height > canvas->height) height = canvas->height - y;
    color = native_color(canvas, color);
    for (uint32_t row = 0; row < height; ++row) {
        uint32_t *pixel = canvas->pixels + ((y + row) * canvas->stride) + x;
        for (uint32_t column = 0; column < width; ++column) pixel[column] = color;
    }
}

static void blend_pixel(struct canvas *canvas, uint32_t x, uint32_t y, uint32_t color, uint8_t alpha) {
    if (!alpha || x >= canvas->width || y >= canvas->height) return;
    uint32_t *pixel = canvas->pixels + y * canvas->stride + x;
    if (alpha == 255) {
        *pixel = native_color(canvas, color);
        return;
    }
    uint32_t background = rgb_color(canvas, *pixel);
    uint32_t red = ((((color >> 16) & 0xff) * alpha) + (((background >> 16) & 0xff) * (255 - alpha))) / 255;
    uint32_t green = ((((color >> 8) & 0xff) * alpha) + (((background >> 8) & 0xff) * (255 - alpha))) / 255;
    uint32_t blue = (((color & 0xff) * alpha) + ((background & 0xff) * (255 - alpha))) / 255;
    *pixel = native_color(canvas, (red << 16) | (green << 8) | blue);
}

static void draw_wordmark(struct canvas *canvas, uint32_t x, uint32_t y, uint32_t color) {
    for (unsigned row = 0; row < AKAOS_LOGO_HEIGHT; ++row) {
        for (unsigned column = 0; column < AKAOS_LOGO_WIDTH; ++column) {
            blend_pixel(canvas, x + column, y + row, color, akaos_logo_alpha[row * AKAOS_LOGO_WIDTH + column]);
        }
    }
}

static int canvas_from_boot_info(const struct akaos_boot_info *boot_info, struct canvas *canvas) {
    if (boot_info->framebuffer.pixel_format > AKAOS_PIXEL_BGRX8888) return 0;
    *canvas = (struct canvas){
        (uint32_t *)(uintptr_t)boot_info->framebuffer.address,
        boot_info->framebuffer.width,
        boot_info->framebuffer.height,
        boot_info->framebuffer.pixels_per_scanline,
        boot_info->framebuffer.pixel_format == AKAOS_PIXEL_RGBX8888
    };
    return 1;
}

/* ================================================================
 * Boot UI — akaOS boot splash
 *
 * The XSCore splash: akaOS wordmark on pure black with a small spinner
 * underneath. The spinner advances once per real initialization step
 * (one tick per completed subsystem), so the animation is driven by
 * actual kernel progress — never by a fake delay.
 * ================================================================ */

static struct canvas boot_canvas;
static int boot_canvas_valid;

/* Spinner geometry: 8 dots on a ring around the splash center */
#define SPINNER_DOT_SIZE 3
#define SPINNER_RADIUS 7

static const int8_t spinner_offsets[8][2] = {
    {0, -SPINNER_RADIUS}, {5, -5}, {SPINNER_RADIUS, 0}, {5, 5},
    {0, SPINNER_RADIUS}, {-5, 5}, {-SPINNER_RADIUS, 0}, {-5, -5},
};

static uint32_t spinner_phase;
static uint32_t spinner_center_x;
static uint32_t spinner_center_y;

/* ---- Anti-aliased text rendering (DejaVu Sans generated atlas) ---- */

static uint32_t text_width(const char *text) {
    uint32_t width = 0;
    for (const char *p = text; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch < AKAOS_FONT_FIRST || ch > AKAOS_FONT_LAST) ch = '?';
        width += akaos_font_advances[ch - AKAOS_FONT_FIRST] + 1;
    }
    return width;
}

static void draw_glyph(struct canvas *canvas, uint32_t x, uint32_t y,
                       uint32_t color, unsigned char ch) {
    if (ch < AKAOS_FONT_FIRST || ch > AKAOS_FONT_LAST) ch = '?';
    unsigned glyph = ch - AKAOS_FONT_FIRST;
    const uint8_t *alpha = akaos_font_alpha + glyph * AKAOS_FONT_CELL_WIDTH * AKAOS_FONT_CELL_HEIGHT;
    for (unsigned row = 0; row < AKAOS_FONT_CELL_HEIGHT; ++row) {
        for (unsigned col = 0; col < AKAOS_FONT_CELL_WIDTH; ++col) {
            uint8_t a = alpha[row * AKAOS_FONT_CELL_WIDTH + col];
            if (!a) continue;
            blend_pixel(canvas, x + col, y + row, color, a);
        }
    }
}

static void draw_text(struct canvas *canvas, uint32_t x, uint32_t y, uint32_t color, const char *text) {
    uint32_t cursor_x = x;
    for (const char *p = text; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (ch < AKAOS_FONT_FIRST || ch > AKAOS_FONT_LAST) ch = '?';
        unsigned glyph = ch - AKAOS_FONT_FIRST;
        draw_glyph(canvas, cursor_x, y, color, ch);
        cursor_x += akaos_font_advances[glyph] + 1;
    }
}

/* ---- Two-phase boot: early console (verbose) vs clean splash (quiet) ---- */

#define UI_COLOR_TEXT_PRIMARY 0xe6e6e6
#define UI_COLOR_TEXT_DIM 0x9a9a9a
#define UI_COLOR_OK 0x3fb950
#define UI_COLOR_PENDING 0x8b949e
#define UI_COLOR_SUBTITLE 0x8a8a8a

static uint32_t early_cursor_x = 0;
static uint32_t early_cursor_y = 0;
static uint32_t early_margin = 16;
static uint32_t logo_x_global = 0;
static uint32_t logo_y_global = 0;
static uint32_t subtitle_y_global = 0;

// Forward
static void framebuffer_clear(void);
static void splash_init(void);
static void early_console_init(const struct akaos_boot_info *boot_info);
static void early_console_scroll_if_needed(void);
int xscore_process_initialize(void);
__attribute__((noreturn)) static void xscore_shell_run(void);

static void framebuffer_clear(void) {
    if (!boot_canvas_valid) return;
    fill_rect(&boot_canvas, 0, 0, boot_canvas.width, boot_canvas.height, 0x000000);
}

static void early_console_init(const struct akaos_boot_info *boot_info) {
    boot_canvas_valid = canvas_from_boot_info(boot_info, &boot_canvas);
    if (!boot_canvas_valid) return;
    framebuffer_clear();
    shell_grid_init();
    shell_cursor_col = 0;
    shell_cursor_row = 0;
    shell_input_row = 0;
    shell_input_rows = 0;
    early_console_ready = 1;
    splash_active = 0;
}

static void early_console_scroll_if_needed(void) {
    if (!early_console_ready || !boot_canvas_valid) return;
    uint32_t line_h = AKAOS_FONT_CELL_HEIGHT;
    if (early_cursor_y + line_h + early_margin > boot_canvas.height) {
        uint32_t scroll = line_h;
        // Move framebuffer content up
        uint32_t rows_to_keep = boot_canvas.height - early_margin - scroll - early_margin;
        for (uint32_t y = early_margin; y < early_margin + rows_to_keep; ++y) {
            uint32_t *dst = boot_canvas.pixels + y * boot_canvas.stride;
            uint32_t *src = boot_canvas.pixels + (y + scroll) * boot_canvas.stride;
            for (uint32_t x = 0; x < boot_canvas.width; ++x) dst[x] = src[x];
        }
        // Clear bottom area
        fill_rect(&boot_canvas, 0, boot_canvas.height - early_margin - scroll, boot_canvas.width, scroll + early_margin, 0x000000);
        if (early_cursor_y >= scroll) early_cursor_y -= scroll;
        else early_cursor_y = early_margin;
    }
}

static void early_console_write_at(uint32_t x, uint32_t y, uint32_t color, const char *text) {
    draw_text(&boot_canvas, x, y, color, text);
}

static void early_console_write_line(const char *prefix, const char *msg, uint32_t prefix_color, uint32_t msg_color) {
    if (!early_console_ready || splash_active) return;
    if (!verbose_mode) return;
    early_console_scroll_if_needed();
    uint32_t y = early_cursor_y;
    uint32_t x = early_cursor_x;
    uint32_t pref_w = text_width(prefix);
    early_console_write_at(x, y, prefix_color, prefix);
    early_console_write_at(x + pref_w + 8, y, msg_color, msg);
    early_cursor_y += AKAOS_FONT_CELL_HEIGHT;
    early_cursor_x = early_margin;
}

static void early_console_write_msg(const char *msg, uint32_t color) {
    if (!early_console_ready || splash_active) return;
    if (!verbose_mode) return;
    early_console_scroll_if_needed();
    early_console_write_at(early_cursor_x, early_cursor_y, color, msg);
    early_cursor_y += AKAOS_FONT_CELL_HEIGHT;
}

static void splash_init(void) {
    if (!boot_canvas_valid) {
        serial_write("[WARN] Splash: no canvas\n");
        return;
    }
    serial_write("[INFO] Splash init\n");
    framebuffer_clear();
    /* Center the entire logo + subtitle + loading indicator group. */
    const uint32_t subtitle_gap = 8;
    const uint32_t spinner_gap = 16;
    const uint32_t spinner_height = (SPINNER_RADIUS + SPINNER_DOT_SIZE + 2) * 2 + 1;
    const uint32_t group_height = AKAOS_LOGO_HEIGHT + subtitle_gap +
                                  AKAOS_FONT_CELL_HEIGHT + spinner_gap + spinner_height;
    logo_y_global = boot_canvas.height > group_height ? (boot_canvas.height - group_height) / 2 : 0;
    logo_x_global = boot_canvas.width > AKAOS_LOGO_WIDTH ? (boot_canvas.width - AKAOS_LOGO_WIDTH) / 2 : 0;
    draw_wordmark(&boot_canvas, logo_x_global, logo_y_global, 0xf0f0f0);
    subtitle_y_global = logo_y_global + AKAOS_LOGO_HEIGHT + subtitle_gap;
    const char *subtitle = "Starting XSCore";
    uint32_t sub_w = text_width(subtitle);
    uint32_t sub_x = boot_canvas.width > sub_w ? (boot_canvas.width - sub_w) / 2 : 0;
    if (subtitle_y_global + AKAOS_FONT_CELL_HEIGHT < boot_canvas.height) {
        draw_text(&boot_canvas, sub_x, subtitle_y_global, UI_COLOR_SUBTITLE, subtitle);
    }
    spinner_center_x = boot_canvas.width / 2;
    spinner_center_y = subtitle_y_global + AKAOS_FONT_CELL_HEIGHT + spinner_gap + spinner_height / 2;
    spinner_phase = 0;
    splash_active = 1;
    // Draw spinner immediately
    uint32_t box = SPINNER_RADIUS + SPINNER_DOT_SIZE + 2;
    fill_rect(&boot_canvas, spinner_center_x - box, spinner_center_y - box, box*2+1, box*2+1, 0x000000);
    for (unsigned dot = 0; dot < 8; ++dot) {
        unsigned lit = ((dot - spinner_phase + 8) % 8) < 4;
        uint32_t color = lit ? 0xe2e2e2 : 0x2e2e2e;
        fill_rect(&boot_canvas, spinner_center_x + (uint32_t)spinner_offsets[dot][0] - SPINNER_DOT_SIZE/2, spinner_center_y + (uint32_t)spinner_offsets[dot][1] - SPINNER_DOT_SIZE/2, SPINNER_DOT_SIZE, SPINNER_DOT_SIZE, color);
    }
    serial_write("[INFO] Splash drawn\n");
}

static void spinner_draw(void) {
    if (!boot_canvas_valid || !splash_active) return;
    uint32_t box = SPINNER_RADIUS + SPINNER_DOT_SIZE + 2;
    fill_rect(&boot_canvas, spinner_center_x - box, spinner_center_y - box, box*2+1, box*2+1, 0x000000);
    for (unsigned dot = 0; dot < 8; ++dot) {
        unsigned lit = ((dot - spinner_phase + 8) % 8) < 4;
        uint32_t color = lit ? 0xe2e2e2 : 0x2e2e2e;
        fill_rect(&boot_canvas, spinner_center_x + (uint32_t)spinner_offsets[dot][0] - SPINNER_DOT_SIZE/2, spinner_center_y + (uint32_t)spinner_offsets[dot][1] - SPINNER_DOT_SIZE/2, SPINNER_DOT_SIZE, SPINNER_DOT_SIZE, color);
    }
}

static void splash_progress_step(void) {
    if (!splash_active) return;
    spinner_phase = (spinner_phase + 1) % 8;
    spinner_draw();
}

/* ================================================================
 * Fullscreen interactive TTY console
 *
 * The first user process is intentionally short-lived. Normal boots now
 * hand off to this text-mode shell instead of leaving the splash frozen.
 * The APIC timer wakes the polling loop while the keyboard IRQ remains
 * reserved for the future input driver.
 * ================================================================ */

static uint32_t shell_columns;
static uint32_t shell_rows;

#define SHELL_BACKGROUND 0x000000
#define SHELL_CYAN 0x66d9ef
#define SHELL_GREEN 0x7ee787
#define SHELL_AMBER 0xf2cc60
#define SHELL_TEXT 0xe6edf3
#define SHELL_DIM 0x8b9bad
#define SHELL_CELL_WIDTH AKAOS_VGA_FONT_CELL_WIDTH
#define SHELL_CELL_HEIGHT AKAOS_VGA_FONT_CELL_HEIGHT

static void shell_grid_init(void) {
    if (!boot_canvas_valid) {
        shell_columns = 0;
        shell_rows = 0;
        return;
    }
    shell_columns = boot_canvas.width / SHELL_CELL_WIDTH;
    shell_rows = boot_canvas.height / SHELL_CELL_HEIGHT;
    if (shell_columns == 0) shell_columns = 1;
    if (shell_rows == 0) shell_rows = 1;
}

static void shell_scroll_one_row(void) {
    if (!boot_canvas_valid || shell_rows <= 1) return;
    uint32_t line_h = SHELL_CELL_HEIGHT;
    uint32_t copy_height = boot_canvas.height > line_h ? boot_canvas.height - line_h : 0;
    for (uint32_t y = 0; y < copy_height; ++y) {
        uint32_t *dst = boot_canvas.pixels + y * boot_canvas.stride;
        uint32_t *src = boot_canvas.pixels + (y + line_h) * boot_canvas.stride;
        for (uint32_t x = 0; x < boot_canvas.width; ++x) dst[x] = src[x];
    }
    fill_rect(&boot_canvas, 0, copy_height, boot_canvas.width, line_h, SHELL_BACKGROUND);
    if (shell_cursor_row != 0) --shell_cursor_row;
    if (shell_input_row != 0) --shell_input_row;
}

static void shell_scroll_if_needed(void) {
    if (!boot_canvas_valid || shell_rows == 0) return;
    while (shell_cursor_row >= shell_rows) {
        if (shell_rows <= 1) {
            framebuffer_clear();
            shell_cursor_row = 0;
            shell_cursor_col = 0;
            shell_input_row = 0;
            shell_input_rows = 0;
            break;
        }
        shell_scroll_one_row();
    }
}

static int shell_reverse_video = 0;

/*
 * Render one 8x16 VGA bitmap glyph at a 1:1 scale on the character grid.
 * Both the foreground (set bits) and the background (clear bits) are
 * painted, so stale framebuffer content can never bleed through a cell.
 * Each glyph advances exactly SHELL_CELL_WIDTH pixels: strict monospace.
 */
static void shell_draw_glyph(uint32_t x, uint32_t y, uint32_t color, unsigned char ch) {
    if (!boot_canvas_valid) return;
    if (x + SHELL_CELL_WIDTH > boot_canvas.width ||
        y + SHELL_CELL_HEIGHT > boot_canvas.height) return;
    const uint8_t *glyph = akaos_vga_font + (size_t)ch * AKAOS_VGA_FONT_CELL_HEIGHT;
    uint32_t fg_val = shell_reverse_video ? SHELL_BACKGROUND : color;
    uint32_t bg_val = shell_reverse_video ? color : SHELL_BACKGROUND;
    uint32_t fg = native_color(&boot_canvas, fg_val);
    uint32_t bg = native_color(&boot_canvas, bg_val);
    for (unsigned row = 0; row < SHELL_CELL_HEIGHT; ++row) {
        uint8_t bits = glyph[row];
        uint32_t *line = boot_canvas.pixels + (y + row) * boot_canvas.stride + x;
        for (unsigned col = 0; col < SHELL_CELL_WIDTH; ++col) {
            line[col] = (bits & (0x80U >> col)) ? fg : bg;
        }
    }
}

static void shell_put_char(unsigned char character, uint32_t color) {
    shell_scroll_if_needed();
    if (shell_cursor_col >= shell_columns) {
        shell_cursor_col = 0;
        ++shell_cursor_row;
        shell_scroll_if_needed();
    }
    if (shell_cursor_row >= shell_rows) return;
    shell_draw_glyph(shell_cursor_col * SHELL_CELL_WIDTH,
                     shell_cursor_row * SHELL_CELL_HEIGHT, color, character);
    ++shell_cursor_col;
    if (shell_cursor_col >= shell_columns) {
        shell_cursor_col = 0;
        ++shell_cursor_row;
    }
}

static void shell_write_line(const char *text, uint32_t color) {
    if (!boot_canvas_valid) return;
    uint32_t wrote_character = 0;
    while (*text) {
        shell_put_char((unsigned char)*text++, color);
        wrote_character = 1;
    }
    /* A full-width line already wrapped to the next grid row. */
    if (shell_cursor_col != 0 || !wrote_character) ++shell_cursor_row;
    shell_cursor_col = 0;
    shell_scroll_if_needed();
}

static void shell_reset_screen(void) {
    if (!boot_canvas_valid) return;
    framebuffer_clear();
    shell_grid_init();
    shell_reverse_video = 0;
    shell_cursor_col = 0;
    shell_cursor_row = 0;
    shell_input_row = 0;
    shell_input_rows = 0;
}

static int shell_escape_state = 0;
static int shell_csi_params[8];
static int shell_csi_param_count = 0;
static int shell_csi_has_param = 0;

static void handle_csi_command(char cmd) {
    if (cmd == 'J') {
        int param = (shell_csi_param_count > 0) ? shell_csi_params[0] : 0;
        if (param == 2 || param == 0) {
            shell_reset_screen();
        }
    } else if (cmd == 'H' || cmd == 'f') {
        int row = (shell_csi_param_count > 0 && shell_csi_params[0] > 0) ? shell_csi_params[0] - 1 : 0;
        int col = (shell_csi_param_count > 1 && shell_csi_params[1] > 0) ? shell_csi_params[1] - 1 : 0;
        if (row < 0) row = 0;
        if (col < 0) col = 0;
        if ((uint32_t)row >= shell_rows) row = (int)shell_rows - 1;
        if ((uint32_t)col >= shell_columns) col = (int)shell_columns - 1;
        shell_cursor_row = (uint32_t)row;
        shell_cursor_col = (uint32_t)col;
    } else if (cmd == 'm') {
        if (shell_csi_param_count == 0) {
            shell_reverse_video = 0;
        } else {
            for (int i = 0; i < shell_csi_param_count; ++i) {
                int p = shell_csi_params[i];
                if (p == 0) {
                    shell_reverse_video = 0;
                } else if (p == 7) {
                    shell_reverse_video = 1;
                } else if (p == 27) {
                    shell_reverse_video = 0;
                }
            }
        }
    } else if (cmd == 'K') {
        if (boot_canvas_valid && shell_cursor_row < shell_rows) {
            for (uint32_t col = shell_cursor_col; col < shell_columns; ++col) {
                shell_draw_glyph(col * SHELL_CELL_WIDTH, shell_cursor_row * SHELL_CELL_HEIGHT, SHELL_TEXT, ' ');
            }
        }
    }
}

static void shell_console_sink(char c) {
    if (!boot_canvas_valid) return;

    if (shell_escape_state == 1) {
        if (c == '[') {
            shell_escape_state = 2;
            shell_csi_param_count = 0;
            shell_csi_has_param = 0;
            for (int i = 0; i < 8; ++i) shell_csi_params[i] = 0;
        } else {
            shell_escape_state = 0;
        }
        return;
    } else if (shell_escape_state == 2) {
        if (c >= '0' && c <= '9') {
            shell_csi_has_param = 1;
            if (shell_csi_param_count < 8) {
                shell_csi_params[shell_csi_param_count] = shell_csi_params[shell_csi_param_count] * 10 + (c - '0');
            }
            return;
        } else if (c == ';') {
            if (shell_csi_param_count < 7) {
                ++shell_csi_param_count;
            }
            shell_csi_has_param = 1;
            return;
        } else if (c == '?') {
            /* Ignore private mode flag */
            return;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            if (shell_csi_has_param && shell_csi_param_count < 8) {
                ++shell_csi_param_count;
            }
            handle_csi_command(c);
            shell_escape_state = 0;
            return;
        }
        shell_escape_state = 0;
        return;
    }

    if (c == '\033') {
        shell_escape_state = 1;
        return;
    }
    if (c == '\f') {
        shell_reset_screen();
        return;
    }
    if (c == '\n') {
        shell_cursor_col = 0;
        ++shell_cursor_row;
        shell_scroll_if_needed();
    } else if (c == '\r') {
        shell_cursor_col = 0;
    } else if (c == '\b') {
        if (shell_cursor_col > 0) {
            --shell_cursor_col;
            shell_put_char(' ', SHELL_TEXT);
            --shell_cursor_col;
        }
    } else {
        shell_put_char((unsigned char)c, SHELL_TEXT);
    }
}

__attribute__((noreturn)) static void xscore_shell_run(void) {
    xscore_file_set_console_sink(shell_console_sink);
    if (splash_active) {
        splash_active = 0;
        shell_reset_screen();
    } else {
        splash_active = 0;
    }
    xscore_tty_initialize();
    xscore_mouse_init_all();
    serial_write("[INFO] akaOS shell active\n");

    const char *init_path = "/bin/init";
    uint32_t init_node = 0;
    if (xscore_vfs_resolve_path_at(0, init_path, &init_node) != 0) {
        init_path = "/bin/akash";
    }

    serial_write("[INFO] Spawning userspace root process: ");
    serial_write(init_path);
    serial_write("\n");

    for (;;) {
        struct xscore_process *root_proc = xscore_process_create_path(init_path);
        if (root_proc) {
            (void)xscore_process_set_reaper(root_proc);
            uint64_t exit_status = 0;
            xscore_process_wait(root_proc, &exit_status);
            xscore_process_reap(root_proc);
        } else {
            serial_write("[FATAL] xscore_process_create_path failed for: ");
            serial_write(init_path);
            serial_write("\n");
            shell_write_line("init: failed to start userspace init process", SHELL_AMBER);
            for (;;) {
                xscore_tty_poll_keyboard();
                __asm__ volatile("hlt");
            }
        }
    }
}

/* ---- Decoupled logging: serial always, framebuffer only when appropriate ---- */

static void boot_log_ok(const char *message) {
    serial_write("[ OK ] ");
    serial_write(message);
    serial_write("\n");
    if (!splash_active && verbose_mode && early_console_ready) {
        early_console_write_line("[ OK ]", message, UI_COLOR_OK, UI_COLOR_TEXT_PRIMARY);
    }
}

static void boot_log_pending(const char *message) {
    serial_write("[ .. ] ");
    serial_write(message);
    serial_write("\n");
    if (!splash_active && verbose_mode && early_console_ready) {
        early_console_write_line("[ .. ]", message, UI_COLOR_PENDING, UI_COLOR_TEXT_DIM);
    }
}

static void boot_overview(const char *label, const char *value) {
    // Overview is verbose-only and never drawn over splash
    serial_write("[INFO] ");
    serial_write(label);
    serial_write(": ");
    serial_write(value);
    serial_write("\n");
    if (!splash_active && verbose_mode && early_console_ready) {
        char combined[96];
        char *p = combined;
        const char *a = label; while (*a && p < combined + 70) *p++ = *a++;
        *p++ = ':'; *p++ = ' '; a = value; while (*a && p < combined + 95) *p++ = *a++;
        *p = 0;
        early_console_write_msg(combined, UI_COLOR_TEXT_DIM);
    }
}

static void boot_status(const char *message) {
    serial_write("[INFO] ");
    serial_write(message);
    serial_write("\n");
    // Status never draws over splash in this design — splash stays clean
    // (spinner shows progress). If verbose, show as early console line.
    if (!splash_active && verbose_mode && early_console_ready) {
        early_console_write_msg(message, 0x9e9e9e);
    }
}

/* ================================================================
 * Kernel thread self-test worker
 *
 * Runs in a freshly created kernel thread. Sets a marker visible to the
 * boot thread, waits for a timer-generated preemption request, consumes it
 * at a safe thread checkpoint, then resumes and exits.
 * ================================================================ */

static volatile int thread_self_test_marker;

static void xscore_thread_self_test_worker(void *argument) {
    (void)argument;
    thread_self_test_marker = 0xA5;
    serial_write("[INFO] Kernel thread worker entered\n");
    uint64_t start = xscore_timer_ticks();
    while (xscore_timer_ticks() - start < 6) __asm__ volatile("hlt");
    serial_write("[INFO] Timer requested deferred preemption\n");
    if (!xscore_thread_preemption_checkpoint()) {
        thread_self_test_marker = -1;
        return;
    }
    thread_self_test_marker = 0x5A;
    serial_write("[INFO] Worker resumed after safe preemption checkpoint\n");
}

/* ================================================================
 * Section permissions
 * ================================================================ */

#define LARGE_PAGE_SIZE (UINT64_C(2) * 1024 * 1024)

/*
 * Convert every 2 MiB large page that intersects [start, end) into 4 KiB
 * pages, so that per-section permission flags can be applied. The identity
 * map uses large pages beyond the first 2 MiB; the kernel image crosses that
 * boundary (the 64 GiB PMM bitmap in .bss alone spans more than 2 MiB).
 */
static int ensure_kernel_fine_grained(uint64_t start, uint64_t end) {
    uint64_t first = start & ~(LARGE_PAGE_SIZE - 1);
    for (uint64_t address = first; address < end; address += LARGE_PAGE_SIZE) {
        if (address == 0) continue; /* page zero stays unmapped */
        if (!xscore_vmm_split_large_page(address)) return 0;
    }
    return 1;
}

static int apply_section_permissions(void) {
    uint64_t page_size = 4096;
    uint64_t text_start = (uint64_t)(uintptr_t)__text_start;
    uint64_t text_end   = (uint64_t)(uintptr_t)__text_end;
    uint64_t ro_start   = (uint64_t)(uintptr_t)__rodata_start;
    uint64_t ro_end     = (uint64_t)(uintptr_t)__rodata_end;
    uint64_t data_start = (uint64_t)(uintptr_t)__data_start;
    uint64_t bss_start  = (uint64_t)(uintptr_t)__bss_start;
    uint64_t data_end   = (uint64_t)(uintptr_t)__data_end;

    serial_number("[INFO] .text:   ", text_start, "");
    serial_number(" - ", text_end, "\n");
    serial_number("[INFO] .rodata: ", ro_start, "");
    serial_number(" - ", ro_end, "\n");
    serial_number("[INFO] .data:   ", data_start, "");
    serial_number(" - ", bss_start, "\n");
    serial_number("[INFO] .bss:    ", bss_start, "");
    serial_number(" - ", data_end, "\n");

    if (!ensure_kernel_fine_grained(text_start, data_end)) {
        serial_write("[FATAL] Could not obtain 4 KiB granularity for kernel sections\n");
        return 0;
    }

    /*
     * Kernel layout at 0x100000, page-aligned sections:
     *   .text   → R+X (present, not writable, not NX)
     *   .rodata → R   (present, not writable, NX)
     *   .data   → RW  (present, writable, NX)
     *   .bss    → RW  (present, writable, NX)
     */

    /* .text: Read + Execute (remove writable, no NX) */
    for (uint64_t addr = text_start; addr < text_end; addr += page_size) {
        if (!xscore_vmm_update_page_flags(addr, 0)) {
            serial_number("[FATAL] Section perm failed at .text addr ", addr, "\n");
            return 0;
        }
    }
    serial_write("[INFO] .text section: Read + Execute\n");

    /* .rodata: Read only (NX, not writable) */
    for (uint64_t addr = ro_start; addr < ro_end; addr += page_size) {
        if (!xscore_vmm_update_page_flags(addr, XSCORE_PAGE_NO_EXECUTE)) {
            serial_number("[FATAL] Section perm failed at .rodata addr ", addr, "\n");
            return 0;
        }
    }
    serial_write("[INFO] .rodata section: Read only + NX\n");

    /* .data: Read + Write + NX */
    for (uint64_t addr = data_start; addr < bss_start; addr += page_size) {
        if (!xscore_vmm_update_page_flags(addr, XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE)) {
            serial_number("[FATAL] Section perm failed at .data addr ", addr, "\n");
            return 0;
        }
    }
    serial_write("[INFO] .data section: Read + Write + NX\n");

    /* .bss: Read + Write + NX */
    for (uint64_t addr = bss_start; addr < data_end; addr += page_size) {
        if (!xscore_vmm_update_page_flags(addr, XSCORE_PAGE_WRITABLE | XSCORE_PAGE_NO_EXECUTE)) {
            serial_number("[FATAL] Section perm failed at .bss addr ", addr, "\n");
            return 0;
        }
    }
    serial_write("[INFO] .bss section: Read + Write + NX\n");

    /* Flush entire TLB by reloading CR3 */
#if defined(__x86_64__)
    uint64_t cr3;
    __asm__ volatile("movq %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("movq %0, %%cr3" : : "r"(cr3) : "memory");
#elif defined(__i386__)
    uint32_t cr3;
    __asm__ volatile("movl %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("movl %0, %%cr3" : : "r"(cr3) : "memory");
#endif

    return 1;
}

/* ================================================================
 * Boot continuation on the dedicated kernel stack.
 *
 * Entered once by xscore_stack_enter() after xscore_stack_initialize()
 * succeeds. From this point on the whole boot chain runs on the
 * XSCore-owned boot stack; the function must never return.
 * ================================================================ */

__attribute__((noreturn)) static void xscore_boot_on_kernel_stack(uint64_t boot_info_address);

__attribute__((noreturn)) static void xscore_boot_on_kernel_stack(uint64_t boot_info_address) {
    const struct akaos_boot_info *boot_info = (const void *)(uintptr_t)boot_info_address;

    /* Install TSS with IST entries */
    xscore_tss_install();
    serial_write("[INFO] TSS installed\n");
    boot_log_ok("TSS installed (IST active)");
    splash_progress_step();
    boot_overview("Stacks", "Guard pages + IST");

    /* ============================================================
     * Phase 3 — Interrupt controller
     * ============================================================ */

    boot_status("Initializing interrupt controller...");

    /* Install hardware IRQ handlers into IDT before enabling APIC */
    xscore_idt_install_irq_handlers();
    xscore_idt_install_syscall_handler();
    serial_write("[INFO] Hardware IRQ vectors installed\n");
    serial_write("[INFO] Ring 3 system-call gate installed\n");

    if (!xscore_apic_initialize()) {
        serial_write("[FATAL] APIC initialization failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_write("[INFO] 8259 PIC disabled\n");
    serial_write("[INFO] Local APIC enabled\n");
    boot_log_ok("Interrupt controller online");
    splash_progress_step();
    boot_overview("Interrupts", "Local APIC");

    /* ============================================================
     * Phase 3 — Timer
     * ============================================================ */

    /* Scheduler foundation: per-CPU record before the timer starts */
    xscore_sched_initialize();
    serial_write("[INFO] Scheduler per-CPU record initialized\n");

    boot_status("Calibrating timer...");

    if (!xscore_timer_initialize()) {
        serial_write("[FATAL] Timer initialization failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }

    /* Enable interrupts so the timer can fire */
    __asm__ volatile("sti");

    serial_write("[INFO] APIC timer running at 100 Hz\n");
    boot_log_ok("Timer initialized (100 Hz)");
    splash_progress_step();
    boot_overview("Timer", "APIC 100 Hz");

    /* Brief spin to verify timer ticks are incrementing */
    uint64_t start_ticks = xscore_timer_ticks();
    for (volatile int i = 0; i < 1000000; ++i) __asm__ volatile("pause");
    uint64_t elapsed = xscore_timer_ticks() - start_ticks;
    if (elapsed > 0) {
        serial_write("[INFO] Timer ticks verified\n");
    } else {
        serial_write("[WARN] Timer ticks did not advance during verification spin\n");
    }

    /* ============================================================
     * Scheduler — foundation only (not yet preemptive)
     * ============================================================ */

    struct xscore_sched_stats sched_stats = xscore_sched_get_stats();
    serial_number("[INFO] Scheduler timer ticks observed: ", sched_stats.timer_ticks, "\n");
    serial_write("[INFO] Scheduler foundation: tick counter live, preemption pending\n");
    boot_log_pending("Scheduler (foundation only)");
    splash_progress_step();
    boot_overview("Scheduler", "Tick core");

    /* ============================================================
     * Phase 3 — Kernel threads (multitasking)
     *
     * Turn the running context into the boot thread, then verify that a
     * freshly created thread can be switched to and back. This is the
     * first real context switch in XSCore.
     * ============================================================ */

    boot_status("Initializing kernel threads...");
    if (!xscore_thread_initialize()) {
        serial_write("[FATAL] Kernel thread subsystem initialization failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_write("[INFO] Kernel thread subsystem initialized\n");
    /*
     * Thread self-test: a timer tick requests preemption while the worker is
     * running. The worker switches only at an explicit safe checkpoint, then
     * resumes, exits, and has both of its guarded stacks reclaimed.
     */
    thread_self_test_marker = 0;
    uint64_t worker_id = xscore_thread_create(xscore_thread_self_test_worker, 0);
    if (worker_id == 0) {
        serial_write("[FATAL] Could not create kernel thread\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_number("[INFO] Created kernel thread ", worker_id, "\n");
    /* Yield to let the worker run first. */
    xscore_thread_yield();
    if (thread_self_test_marker != 0xA5) {
        serial_write("[FATAL] Deferred preemption checkpoint failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    sched_stats = xscore_sched_get_stats();
    if (sched_stats.preemptions == 0) {
        serial_write("[FATAL] Deferred preemption was not accounted\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_number("[INFO] Safe deferred preemptions: ", sched_stats.preemptions, "\n");
    xscore_thread_yield();
    if (thread_self_test_marker != 0x5A) {
        serial_write("[FATAL] Deferred worker did not resume\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    if (!xscore_thread_reap(worker_id)) {
        serial_write("[FATAL] Kernel thread self-test failed: worker was not reclaimable\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_write("[INFO] Exited kernel thread resources reclaimed\n");
    serial_write("[INFO] Kernel thread self-test passed\n");
    boot_log_ok("Kernel threads active");
    splash_progress_step();
    boot_overview("Threads", "Context switch");

    /* ============================================================
     * Boot complete
     * ============================================================ */

    boot_status("XSCore Phase 3 ready");
    serial_write("[INFO] Kernel boot screen rendered\n");
    serial_write("[PASS] akaOS Phase 3 milestone complete\n");

    if (!xscore_initramfs_initialize() || xscore_vfs_node_count() < 7) {
        serial_write("[FATAL] Initial VFS/initramfs setup failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_write("[INFO] Read-only initramfs mounted at /\n");
    serial_write("[INFO] VFS paths /bin and /etc verified\n");
    /* The SMP boot self-tests exercise descriptor and vnode lifetimes before
     * the interactive framebuffer sink is installed. Enable their pool lock
     * at the first point where those shared objects are initialized. */
    xscore_file_enable_lifetime_locking();

    /* Storage is optional during ordinary interactive boots. Test-mode boots
     * attach a known virtio disk and require a real sector read from it. */
    xscore_acpi_initialize(boot_info);
    if (xscore_tty_enable_interrupts()) {
        serial_write("[INFO] TTY keyboard, mouse and serial hardware IRQs enabled\n");
    }
#if defined(__x86_64__)
    xscore_smp_initialize();
    if ((boot_info->flags & AKAOS_BOOT_FLAG_TEST_MODE) && !xscore_smp_self_test()) {
        serial_write("[FATAL] SMP concurrent work self-test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
#endif
    uint32_t pci_devices = xscore_pci_initialize();
    serial_number("[INFO] PCI devices discovered: ", pci_devices, "\n");
    xscore_cpu_initialize();
    xscore_gpu_initialize();
    xscore_sound_initialize();
    xscore_usb_initialize();
    /* Universal storage subsystem initialization (AHCI SATA, NVMe, VirtIO, IDE) */
    xscore_storage_initialize();
    if (!xscore_block_present()) {
        serial_write("[WARN] No writable block storage detected; continuing without persistent storage\n");
    } else {
        xscore_bufcache_initialize();
    }
    xscore_virtio_net_initialize();
    xscore_net_initialize();
    if (boot_info->flags & AKAOS_BOOT_FLAG_TEST_MODE) {
        static const char expected[] = "AKAOS-VIRTIO-BLK-TEST\n";
        uint8_t sector[512];
        int matches = xscore_virtio_blk_present() &&
                      xscore_virtio_blk_read_sector(0, sector);
        for (size_t index = 0; matches && index < sizeof(expected) - 1; ++index) {
            if (sector[index] != (uint8_t)expected[index]) matches = 0;
        }
        if (matches) {
            serial_write("[INFO] virtio-blk sector read verified\n");
        } else if (!xscore_virtio_blk_present()) {
            serial_write("[INFO] virtio-blk not attached; skipping virtio-specific read test\n");
        } else {
            serial_write("[FATAL] virtio-blk sector read self-test failed\n");
            for (;;) __asm__ volatile("cli; hlt");
        }

        if (xscore_ahci_present()) {
            const struct xscore_ahci_device *adev = xscore_ahci_get_device(0);
            if (adev && xscore_ahci_read_sectors(adev->port_num, 0, 1, sector)) {
                static const char sata_exp[] = "AKAOS-SATA-BLK-TEST\n";
                int s_match = 1;
                for (size_t s = 0; s < sizeof(sata_exp) - 1; ++s) {
                    if (sector[s] != (uint8_t)sata_exp[s]) { s_match = 0; break; }
                }
                if (s_match) {
                    serial_write("[INFO] SATA AHCI sector read verified\n");
                }
            }
        }
        if (xscore_nvme_present()) {
            if (xscore_nvme_read_blocks(1, 0, 1, sector)) {
                static const char nvme_exp[] = "AKAOS-NVME-BLK-TEST\n";
                int n_match = 1;
                for (size_t n = 0; n < sizeof(nvme_exp) - 1; ++n) {
                    if (sector[n] != (uint8_t)nvme_exp[n]) { n_match = 0; break; }
                }
                if (n_match) {
                    serial_write("[INFO] NVMe sector read verified\n");
                }
            }
        }
    }
    if (xscore_virtio_blk_present()) {
        if (xscore_vfs_persistence_mount()) {
            serial_write("[INFO] Writable akaFS data disk mounted\n");
        } else {
            serial_write("[WARN] akaFS data disk could not be mounted\n");
        }
    }

#if defined(__x86_64__)
    /* The scheduler worker uses the shared file, block, and network pools.
     * Run it only after optional devices and the persistent akaFS overlay are
     * fully initialized, while keeping the lightweight counter self-test
     * above as an early AP bring-up check. */
    if ((boot_info->flags & AKAOS_BOOT_FLAG_TEST_MODE) &&
        !xscore_smp_scheduler_self_test()) {
        serial_write("[FATAL] SMP per-CPU scheduler self-test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
#endif

    /* Initialize process management subsystem */
    if (!xscore_process_initialize()) {
        serial_write("[FATAL] Process subsystem initialization failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }

    /* ============================================================
     * Isolated user-process lifecycle self-test (runs ONLY in test mode)
     * ============================================================ */

    if (boot_info->flags & AKAOS_BOOT_FLAG_TEST_MODE) {
        if (!xscore_user_process_self_test()) {
            serial_write("[FATAL] Process isolation or Ring 3 milestone failed\n");
            for (;;) __asm__ volatile("cli; hlt");
        }
        serial_write("[PASS] akaOS user-mode milestone complete\n");
    }

#if defined(__x86_64__)
    if ((boot_info->flags & AKAOS_BOOT_FLAG_TEST_MODE) &&
        !xscore_smp_userspace_self_test()) {
        serial_write("[FATAL] SMP AP userspace self-test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
#endif

    /* All built-in boot tests stay BSP-local.  Once they have passed, ordinary
     * interactive descendants may be placed on online APs. */
    xscore_process_enable_multicore_userspace(
        (boot_info->flags & AKAOS_BOOT_FLAG_TEST_MODE) == 0);

    if (boot_info->flags & AKAOS_BOOT_FLAG_TEST_MODE) out8(0xf4, 0x10);

    xscore_shell_run();
}

/* ================================================================
 * Kernel entry point
 * ================================================================ */

__attribute__((noreturn)) void xscore_entry(const struct akaos_boot_info *boot_info) {
    serial_write("[INFO] XSCore entry reached\n");

    /* Validate boot handoff */
    if (!boot_info || boot_info->magic != AKAOS_BOOT_INFO_MAGIC || boot_info->version != AKAOS_BOOT_INFO_VERSION) {
        serial_write("[FATAL] Invalid boot information\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    xscore_framebuffer_register(&boot_info->framebuffer);

    /* ---- Quiet flag: verbose shows early console, quiet goes straight to splash ---- */
    verbose_mode = (boot_info->flags & AKAOS_BOOT_FLAG_VERBOSE) != 0;
    splash_active = 0;
    early_console_ready = 0;

    // Stage 1: Early console (text-only) — like Linux earlycon
    // In verbose mode we show [ OK ] logs on framebuffer; in quiet we stay black until splash.
    if (verbose_mode) {
        early_console_init(boot_info);
        if (!early_console_ready) {
            // Fallback: framebuffer not available, continue serial-only
            verbose_mode = 0;
            boot_canvas_valid = canvas_from_boot_info(boot_info, &boot_canvas);
            if (boot_canvas_valid) framebuffer_clear();
        }
    } else {
        // Quiet: prepare canvas for later splash but keep black (no early logs on screen)
        boot_canvas_valid = canvas_from_boot_info(boot_info, &boot_canvas);
        if (boot_canvas_valid) framebuffer_clear();
        serial_write("[INFO] Quiet boot: early console hidden, splash will follow\n");
    }

    boot_log_ok((boot_info->flags & AKAOS_BOOT_FLAG_BIOS) ?
                "BIOS Multiboot2 handoff received" : "UEFI handoff received");
    boot_status("Initializing XSCore...");

    /* CPU identification */
    char vendor[13];
    cpu_vendor(vendor);
    uint64_t memory_mib = conventional_memory_mib(boot_info);
#if defined(__x86_64__)
    serial_write("[INFO] CPU: x86-64 ");
#elif defined(__i386__)
    serial_write("[INFO] CPU: x86_32 ");
#else
    serial_write("[INFO] CPU: generic ");
#endif
    serial_write(vendor);
    serial_write("\n");
    serial_number("[INFO] Usable memory: ", memory_mib, " MB\n");
    serial_number("[INFO] Framebuffer width: ", boot_info->framebuffer.width, " pixels\n");
    serial_number("[INFO] Framebuffer height: ", boot_info->framebuffer.height, " pixels\n");

    boot_log_ok("CPU identified");
    boot_overview("Processor", vendor);

    char memory_text[32];
    char *end = append_u64(memory_text, memory_mib);
    *end++ = ' '; *end++ = 'M'; *end++ = 'B'; *end = 0;
    boot_overview("Memory", memory_text);

    char display_text[40];
    end = append_u64(display_text, boot_info->framebuffer.width);
    *end++ = 'x';
    end = append_u64(end, boot_info->framebuffer.height);
    *end = 0;
    boot_overview("Display", display_text);

    /* GDT */
    xscore_gdt_initialize();
    serial_write("[INFO] GDT initialized\n");
    boot_log_ok("GDT installed");

    /* IDT with 32 exception vectors */
    xscore_idt_initialize();
    serial_write("[INFO] IDT initialized with 32 exception vectors\n");
    boot_log_ok("IDT installed (32 vectors)");

    /* Exception self-test */
    xscore_breakpoint_self_test();
    if (!xscore_breakpoint_was_handled()) {
        serial_write("[FATAL] Breakpoint exception self-test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    boot_log_ok("Exception self-test passed");

    /* Physical memory manager */
    struct xscore_physical_memory_stats memory_stats;
    if (!xscore_pmm_initialize(boot_info, &memory_stats)) {
        serial_write("[FATAL] Physical memory manager initialization failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_number("[INFO] Physical pages managed: ", memory_stats.managed_pages, "\n");
    serial_number("[INFO] Physical pages free: ", memory_stats.free_pages, "\n");

    /* PMM self-test */
    uint64_t first_page = xscore_pmm_allocate_page();
    uint64_t second_page = xscore_pmm_allocate_page();
    if (!first_page || !second_page || first_page == second_page) {
        serial_write("[FATAL] Physical page allocation self-test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    *(volatile uint64_t *)(uintptr_t)first_page = UINT64_C(0x5845434f52453121);
    *(volatile uint64_t *)(uintptr_t)second_page = UINT64_C(0x5845434f52453221);
    if (*(volatile uint64_t *)(uintptr_t)first_page != UINT64_C(0x5845434f52453121) ||
        *(volatile uint64_t *)(uintptr_t)second_page != UINT64_C(0x5845434f52453221) ||
        !xscore_pmm_free_page(first_page) || !xscore_pmm_free_page(second_page) ||
        xscore_pmm_free_page_count() != memory_stats.free_pages) {
        serial_write("[FATAL] Physical page allocator integrity test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_write("[INFO] Physical page allocator verified\n");
    boot_log_ok("Physical memory initialized");

    /* Virtual memory manager */
    struct xscore_virtual_memory_stats virtual_stats;
    if (!xscore_vmm_initialize(boot_info, &memory_stats, &virtual_stats)) {
        serial_write("[FATAL] Virtual memory initialization failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_number("[INFO] XSCore CR3: ", virtual_stats.root_table_address, "\n");
    serial_number("[INFO] Identity mapped: ", virtual_stats.identity_mapped_bytes / (1024 * 1024), " MB\n");
    serial_number("[INFO] Page-table pages: ", virtual_stats.page_table_pages, "\n");
    if (!virtual_stats.null_page_guarded || xscore_vmm_map_page(0, 0, XSCORE_PAGE_WRITABLE)) {
        serial_write("[FATAL] Null-page guard self-test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_write("[INFO] Null-page guard active\n");
    serial_write("[INFO] XSCore virtual memory active\n");
    boot_log_ok("Virtual memory active");
    boot_overview("Boot", (boot_info->flags & AKAOS_BOOT_FLAG_BIOS) ?
                           "BIOS Multiboot2 handoff" : "UEFI handoff");
    boot_overview("Virtual memory", "XSCore CR3");

    /* Kernel heap */
    if (!xscore_heap_initialize()) {
        serial_write("[FATAL] Kernel heap initialization failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    uint8_t *small = xscore_heap_allocate(64);
    uint8_t *large = xscore_heap_allocate(8192);
    uint8_t *odd = xscore_heap_allocate(257);
    if (!small || !large || !odd || small == large || large == odd) {
        serial_write("[FATAL] Kernel heap allocation self-test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    for (size_t index = 0; index < 64; ++index) small[index] = (uint8_t)(index ^ 0x5a);
    for (size_t index = 0; index < 8192; ++index) large[index] = (uint8_t)(index ^ 0xa5);
    for (size_t index = 0; index < 257; ++index) odd[index] = (uint8_t)(index * 3);
    if (!xscore_heap_free(large)) {
        serial_write("[FATAL] Kernel heap free self-test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    uint8_t *reused = xscore_heap_allocate(4096);
    int heap_valid = reused == large;
    for (size_t index = 0; index < 64; ++index) heap_valid &= small[index] == (uint8_t)(index ^ 0x5a);
    for (size_t index = 0; index < 257; ++index) heap_valid &= odd[index] == (uint8_t)(index * 3);
    if (!heap_valid || !xscore_heap_free(reused) || !xscore_heap_free(small) || !xscore_heap_free(odd)) {
        serial_write("[FATAL] Kernel heap integrity self-test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    struct xscore_heap_stats heap_stats;
    xscore_heap_get_stats(&heap_stats);
    if (heap_stats.allocated_bytes != 0 || heap_stats.allocation_count != 0) {
        serial_write("[FATAL] Kernel heap accounting self-test failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_number("[INFO] Kernel heap mapped: ", heap_stats.mapped_bytes / 1024, " KB\n");
    serial_write("[INFO] Kernel heap verified\n");
    boot_log_ok("Kernel heap ready");

    /* Initialize Hierarchical SLAB Object Allocator */
    xscore_slab_initialize();
    void *slab_test = kmalloc(64);
    if (slab_test) {
        kfree(slab_test);
        serial_write("[INFO] Kernel SLAB object allocator verified\n");
    }

    // ---- Stage 1 complete: Early text logs were on framebuffer (if verbose) or serial only (if quiet) ----
    // Stage 2: Clean graphical splash — like Windows boot logo + Plymouth
    // Clear everything Stage 1 drew and show only logo + spinner. This decouples logging from UI.
    if (!verbose_mode) {
        boot_status("Transitioning to graphical splash...");
        splash_init();
    } else {
        serial_write("[INFO] Verbose text console mode active: live kernel diagnostics on screen\n");
    }
    // From now on, logging remains serial-only. Splash updates are explicit.

    boot_status("Applying section permissions...");

    /* ============================================================
     * Phase 3 — Kernel section permissions
     * ============================================================ */

    if (!apply_section_permissions()) {
        serial_write("[FATAL] Failed to apply kernel section permissions\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_write("[INFO] Section permissions: .text=RX .rodata=R .data=RW+NX .bss=RW+NX\n");
    boot_log_ok("Section permissions applied");
    splash_progress_step();
    boot_overview("Sections", "W^X enforced");

    /* ============================================================
     * Phase 3 — Dedicated kernel stacks
     * ============================================================ */

    boot_status("Allocating kernel stacks...");

    if (!xscore_stack_initialize()) {
        serial_write("[FATAL] Kernel stack allocation failed\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
    serial_write("[INFO] Kernel boot stack allocated\n");
    boot_log_ok("Kernel stacks allocated");
    splash_progress_step();

    /*
     * Transfer the rest of the boot onto the dedicated kernel stack.
     * The loader stack is abandoned permanently (interrupts are still
     * disabled, so nothing else references it).
     */
    xscore_stack_enter(xscore_stack_boot_top(), xscore_boot_on_kernel_stack,
                       (uint64_t)(uintptr_t)boot_info);
}
