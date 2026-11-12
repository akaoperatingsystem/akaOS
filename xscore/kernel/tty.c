#include <xscore/tty.h>
#include <xscore/file.h>
#include <xscore/process.h>
#include <xscore/thread.h>
#include <xscore/serial.h>
#include <xscore/virtio_input.h>
#include <xscore/input.h>
#include <xscore/ps2.h>
#include <xscore/apic.h>
#include <xscore/smp.h>
#if !defined(__x86_64__) && !defined(__i386__)
#include <xscore/device_irq.h>
#endif
#include <akaos/input.h>
#include <akaos/errno.h>
#include <akaos/signal.h>
#include <akaos/termios.h>
#include <xscore/vfs.h>
#include <stddef.h>
#include <stdint.h>

#define TTY_BUF_SIZE 1024
#define TTY_HISTORY_MAX 16
#define TTY_PENDING_SIZE 1024

static char tty_line_buf[TTY_BUF_SIZE];
static size_t tty_line_len = 0;
static size_t tty_read_pos = 0;
static int tty_line_ready = 0;
/*
 * A producer (keyboard/serial IRQ) can deliver the next line before the
 * userspace reader has consumed the current one.  Keep those bytes instead
 * of dropping them when tty_line_ready is set.  This is especially important
 * for QEMU sendkey streams and real UARTs, where a burst can span reads.
 */
static char tty_pending_buf[TTY_PENDING_SIZE];
static size_t tty_pending_head = 0;
static size_t tty_pending_tail = 0;
static size_t tty_pending_count = 0;
static int tty_draining_pending = 0;
static int tty_shift = 0;
static int tty_ctrl = 0;
static int tty_extended = 0;
static int tty_initialized = 0;
static int tty_irq_enabled;
static int tty_echo_enabled = 1;
static struct akaos_termios tty_termios;
static int tty_esc_state = 0;

#if defined(__x86_64__) || defined(__i386__)
static uint8_t ps2_mouse_packet[3];
static uint8_t ps2_mouse_packet_index;
static uint8_t ps2_mouse_buttons;
#endif

static char tty_history[TTY_HISTORY_MAX][TTY_BUF_SIZE];
static int tty_history_count = 0;
static int tty_history_index = -1;
static char tty_saved_current[TTY_BUF_SIZE];
static size_t tty_saved_len = 0;

static void tty_pending_push(char c) {
    if (tty_pending_count >= TTY_PENDING_SIZE) {
        /* Never overwrite an already queued line.  A later keystroke can be
         * retried by the user, while silently corrupting buffered input is
         * much harder to diagnose. */
        return;
    }
    tty_pending_buf[tty_pending_tail] = c;
    tty_pending_tail = (tty_pending_tail + 1) % TTY_PENDING_SIZE;
    ++tty_pending_count;
}

static int tty_pending_pop(char *out) {
    if (tty_pending_count == 0) return 0;
    if (out) *out = tty_pending_buf[tty_pending_head];
    tty_pending_head = (tty_pending_head + 1) % TTY_PENDING_SIZE;
    --tty_pending_count;
    return 1;
}

static const char tty_keymap[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0a] = '9', [0x0b] = '0',
    [0x0c] = '-', [0x0d] = '=', [0x0f] = '\t', [0x10] = 'q', [0x11] = 'w', [0x12] = 'e',
    [0x13] = 'r', [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1a] = '[', [0x1b] = ']', [0x1e] = 'a',
    [0x1f] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
    [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x29] = '`', [0x2b] = '\\', [0x2c] = 'z', [0x2d] = 'x', [0x2e] = 'c',
    [0x2f] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static const char tty_shift_keymap[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0a] = '(', [0x0b] = ')',
    [0x0c] = '_', [0x0d] = '+', [0x0f] = '\t', [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E',
    [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
    [0x18] = 'O', [0x19] = 'P', [0x1a] = '{', [0x1b] = '}', [0x1e] = 'A',
    [0x1f] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2b] = '|', [0x2c] = 'Z', [0x2d] = 'X', [0x2e] = 'C',
    [0x2f] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<',
    [0x34] = '>', [0x35] = '?', [0x39] = ' ',
};

#if defined(__x86_64__) || defined(__i386__)
static inline uint8_t port_in8(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void port_out8(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

int xscore_xscore_ps2_wait_input_clear(void) {
    for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
        if ((port_in8(0x64) & 2) == 0) return 1;
    }
    return 0;
}

int xscore_xscore_ps2_wait_output(uint8_t *value) {
    for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
        if (port_in8(0x64) & 1) {
            if (value) *value = port_in8(0x60);
            return 1;
        }
    }
    return 0;
}

static int ps2_mouse_write(uint8_t value) {
    uint8_t response = 0;
    if (!xscore_ps2_wait_input_clear()) return 0;
    port_out8(0x64, 0xd4);
    if (!xscore_ps2_wait_input_clear()) return 0;
    port_out8(0x60, value);
    return xscore_ps2_wait_output(&response) && response == 0xfa;
}

void xscore_xscore_ps2_flush_output(void) {
    for (uint32_t attempt = 0; attempt < 256 && (port_in8(0x64) & 1); ++attempt)
        (void)port_in8(0x60);
}

static int ps2_write_controller_config(uint8_t config) {
    if (!xscore_ps2_wait_input_clear()) return 0;
    port_out8(0x64, 0x60);
    if (!xscore_ps2_wait_input_clear()) return 0;
    port_out8(0x60, config);
    return xscore_ps2_wait_input_clear();
}

/* PS/2 Mouse logic moved to xscore/kernel/ps2_mouse.c */

    if (relative_y != 0) {
        xscore_input_publish(AKAOS_EV_REL, AKAOS_REL_Y, relative_y);
    }
    if ((buttons ^ ps2_mouse_buttons) & 1) {
        xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_LEFT,
                             (buttons & 1) != 0);
    }
    if ((buttons ^ ps2_mouse_buttons) & 2) {
        xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_RIGHT,
                             (buttons & 2) != 0);
    }
    if ((buttons ^ ps2_mouse_buttons) & 4) {
        xscore_input_publish(AKAOS_EV_KEY, AKAOS_BTN_MIDDLE,
                             (buttons & 4) != 0);
    }
    ps2_mouse_buttons = buttons;
    xscore_input_publish(AKAOS_EV_SYN, AKAOS_SYN_REPORT, 0);
}

#endif

void xscore_tty_initialize(void) {
    xscore_input_initialize();
    tty_line_len = 0;
    tty_read_pos = 0;
    tty_line_ready = 0;
    tty_pending_head = 0;
    tty_pending_tail = 0;
    tty_pending_count = 0;
    tty_draining_pending = 0;
    tty_shift = 0;
    tty_ctrl = 0;
    tty_extended = 0;
    tty_esc_state = 0;
    tty_history_count = 0;
    tty_history_index = -1;
    tty_saved_len = 0;
    tty_echo_enabled = 1;
    tty_termios.c_iflag = AKAOS_ICRNL;
    tty_termios.c_oflag = AKAOS_OPOST | AKAOS_ONLCR;
    tty_termios.c_cflag = 0;
    tty_termios.c_lflag = AKAOS_ISIG | AKAOS_ICANON | AKAOS_ECHO |
                          AKAOS_ECHOE;
    tty_termios.c_cc[AKAOS_VINTR] = 3;
    tty_termios.c_cc[AKAOS_VQUIT] = 28;
    tty_termios.c_cc[AKAOS_VERASE] = 0x7f;
    tty_termios.c_cc[AKAOS_VKILL] = 21;
    tty_termios.c_cc[AKAOS_VEOF] = 4;
    tty_termios.c_cc[AKAOS_VMIN] = 1;
    tty_termios.c_cc[AKAOS_VTIME] = 0;
    tty_termios.c_cc[AKAOS_VSUSP] = 26;
    tty_initialized = 1;
#if defined(__x86_64__) || defined(__i386__)
    uintptr_t ps2_irq_state = xscore_irq_save();
    int ps2_mouse_ready = ps2_mouse_initialize();
    xscore_irq_restore(ps2_irq_state);
    if (ps2_mouse_ready)
        xscore_serial_write("[INFO] PS/2 mouse streaming enabled\n");
    else
        xscore_serial_write("[WARN] PS/2 mouse initialization failed\n");
#endif
}

int xscore_tty_set_echo(int enabled) {
    int previous = tty_echo_enabled;
    tty_echo_enabled = enabled != 0;
    if (tty_echo_enabled) tty_termios.c_lflag |= AKAOS_ECHO;
    else tty_termios.c_lflag &= ~AKAOS_ECHO;
    return previous;
}

void xscore_tty_get_termios(struct akaos_termios *settings) {
    if (settings) *settings = tty_termios;
}

void xscore_tty_set_termios(const struct akaos_termios *settings) {
    if (!settings) return;
    tty_termios = *settings;
    tty_echo_enabled = (tty_termios.c_lflag & AKAOS_ECHO) != 0;
    if ((tty_termios.c_lflag & AKAOS_ICANON) == 0 && tty_line_len > 0) {
        tty_line_ready = 1;
        tty_read_pos = 0;
    }
}

int xscore_tty_has_line(void) {
    return tty_line_ready;
}

static void tty_process_char(char c);

static void tty_drain_pending(void) {
    char c;
    tty_draining_pending = 1;
    while (tty_pending_pop(&c)) {
        tty_process_char(c);
        /* Canonical input is line-oriented: stop as soon as the queued
         * stream contains a complete line.  In raw mode VMIN=1 makes each
         * byte readable, but processing the queued burst in one pass lets a
         * single read return up to its requested size instead of forcing the
         * shell through one syscall per character. */
        if ((tty_termios.c_lflag & AKAOS_ICANON) != 0) {
            if (tty_line_ready) break;
        } else if (tty_line_len >= TTY_BUF_SIZE - 1) {
            break;
        }
    }
    tty_draining_pending = 0;
}

static size_t tty_visual_len(const char *buf, size_t len) {
    size_t col = 0;
    for (size_t i = 0; i < len; ++i) {
        if (buf[i] == '\t') {
            col += 4 - (col % 4);
        } else {
            col++;
        }
    }
    return col;
}

static void tty_refresh_line(const char *old_buf, size_t old_len,
                             const char *new_buf, size_t new_len) {
    struct xscore_fd_table ktable;
    xscore_fd_table_initialize(&ktable);

    size_t old_vis = tty_visual_len(old_buf, old_len);
    size_t new_vis = tty_visual_len(new_buf, new_len);

    /* Move cursor to start of line, erasing old characters */
    for (size_t i = 0; i < old_vis; ++i) {
        char b = '\b';
        xscore_fd_write(&ktable, 1, &b, 1);
    }

    /* Print new line */
    if (new_len > 0) {
        xscore_fd_write(&ktable, 1, new_buf, new_len);
    }

    /* If old line was longer, clear leftover characters on serial/console */
    if (old_vis > new_vis) {
        size_t diff = old_vis - new_vis;
        for (size_t i = 0; i < diff; ++i) {
            char sp = ' ';
            xscore_fd_write(&ktable, 1, &sp, 1);
        }
        for (size_t i = 0; i < diff; ++i) {
            char b = '\b';
            xscore_fd_write(&ktable, 1, &b, 1);
        }
    }

    xscore_fd_table_close_all(&ktable);
}

static void tty_handle_up_arrow(void) {
    if (!tty_echo_enabled || tty_line_ready || tty_history_count == 0) return;

    if (tty_history_index == -1) {
        for (size_t i = 0; i < tty_line_len; ++i) {
            tty_saved_current[i] = tty_line_buf[i];
        }
        tty_saved_len = tty_line_len;
        tty_history_index = tty_history_count - 1;
    } else if (tty_history_index > 0) {
        --tty_history_index;
    }

    char old_buf[TTY_BUF_SIZE];
    size_t old_len = tty_line_len;
    for (size_t i = 0; i < old_len; ++i) {
        old_buf[i] = tty_line_buf[i];
    }

    const char *hist = tty_history[tty_history_index];
    size_t len = 0;
    while (hist[len] && len < TTY_BUF_SIZE - 2) {
        tty_line_buf[len] = hist[len];
        ++len;
    }
    tty_line_len = len;

    tty_refresh_line(old_buf, old_len, tty_line_buf, tty_line_len);
}

static void tty_handle_down_arrow(void) {
    if (!tty_echo_enabled || tty_line_ready || tty_history_index == -1) return;

    char old_buf[TTY_BUF_SIZE];
    size_t old_len = tty_line_len;
    for (size_t i = 0; i < old_len; ++i) {
        old_buf[i] = tty_line_buf[i];
    }

    if (tty_history_index < tty_history_count - 1) {
        ++tty_history_index;
        const char *hist = tty_history[tty_history_index];
        size_t len = 0;
        while (hist[len] && len < TTY_BUF_SIZE - 2) {
            tty_line_buf[len] = hist[len];
            ++len;
        }
        tty_line_len = len;
    } else {
        tty_history_index = -1;
        for (size_t i = 0; i < tty_saved_len; ++i) {
            tty_line_buf[i] = tty_saved_current[i];
        }
        tty_line_len = tty_saved_len;
    }

    tty_refresh_line(old_buf, old_len, tty_line_buf, tty_line_len);
}

static char tty_prompt[128];
static size_t tty_prompt_len = 0;
static int tty_tab_count = 0;

void xscore_tty_record_output(const char *buf, size_t len) {
    if (!buf || len == 0) return;
    for (size_t i = 0; i < len; ++i) {
        char c = buf[i];
        if (c == '\n' || c == '\r') {
            tty_prompt_len = 0;
        } else if (tty_line_len == 0 && tty_prompt_len < sizeof(tty_prompt) - 1) {
            tty_prompt[tty_prompt_len++] = c;
            tty_prompt[tty_prompt_len] = '\0';
        }
    }
}

static void tty_handle_tab(void) {
    if (!tty_echo_enabled || tty_line_ready) return;
    ++tty_tab_count;

    /* Find the start of the word at the end of tty_line_buf */
    size_t word_start = tty_line_len;
    while (word_start > 0) {
        char prev = tty_line_buf[word_start - 1];
        if (prev == ' ' || prev == '\t' || prev == '|' || prev == ';' ||
            prev == '>' || prev == '<') {
            break;
        }
        --word_start;
    }

    char word[128];
    size_t word_len = tty_line_len - word_start;
    if (word_len >= sizeof(word)) word_len = sizeof(word) - 1;
    for (size_t i = 0; i < word_len; ++i) word[i] = tty_line_buf[word_start + i];
    word[word_len] = '\0';

    /* Determine if word is in command position (first token after line start, '|', or ';') */
    int is_command = 1;
    size_t chk = word_start;
    while (chk > 0) {
        --chk;
        char p = tty_line_buf[chk];
        if (p == '|' || p == ';') break;
        if (p != ' ' && p != '\t') {
            is_command = 0;
            break;
        }
    }

    char matches[32][32];
    size_t match_count = 0;
    char completion[128];
    int is_dir = 0;

    uint32_t cwd_node = 0;
    struct xscore_process *proc = xscore_process_current();
    if (proc) {
        cwd_node = xscore_process_cwd_node(proc);
    }

    int res = xscore_vfs_autocomplete(cwd_node, word, is_command,
                                      completion, sizeof(completion),
                                      &is_dir, matches, 32, &match_count);

    if (res == 0 || match_count == 0) {
        /* No match: leave line untouched, ring bell */
        struct xscore_fd_table ktable;
        xscore_fd_table_initialize(&ktable);
        char bell = '\a';
        xscore_fd_write(&ktable, 1, &bell, 1);
        xscore_fd_table_close_all(&ktable);
        return;
    }

    if (match_count == 1) {
        /* Unique match */
        tty_tab_count = 0;
        size_t compl_len = 0;
        while (completion[compl_len]) compl_len++;

        if (compl_len > 0 && tty_line_len + compl_len < TTY_BUF_SIZE - 2) {
            for (size_t i = 0; i < compl_len; ++i) {
                tty_line_buf[tty_line_len++] = completion[i];
            }
            struct xscore_fd_table ktable;
            xscore_fd_table_initialize(&ktable);
            xscore_fd_write(&ktable, 1, completion, compl_len);
            xscore_fd_table_close_all(&ktable);
        }
        return;
    }

    /* Multiple matches */
    size_t compl_len = 0;
    while (completion[compl_len]) compl_len++;

    if (compl_len > 0) {
        /* Extend to longest common prefix */
        tty_tab_count = 0;
        if (tty_line_len + compl_len < TTY_BUF_SIZE - 2) {
            for (size_t i = 0; i < compl_len; ++i) {
                tty_line_buf[tty_line_len++] = completion[i];
            }
            struct xscore_fd_table ktable;
            xscore_fd_table_initialize(&ktable);
            xscore_fd_write(&ktable, 1, completion, compl_len);
            xscore_fd_table_close_all(&ktable);
        }
        return;
    }

    /* Common prefix already matched. On double-tab, display choices like Linux */
    if (tty_tab_count >= 2) {
        tty_tab_count = 0;
        struct xscore_fd_table ktable;
        xscore_fd_table_initialize(&ktable);

        char nl = '\n';
        xscore_fd_write(&ktable, 1, &nl, 1);
        for (size_t i = 0; i < match_count; ++i) {
            size_t mlen = 0;
            while (matches[i][mlen]) mlen++;
            xscore_fd_write(&ktable, 1, matches[i], mlen);
            char sp2[] = "  ";
            xscore_fd_write(&ktable, 1, sp2, 2);
        }
        xscore_fd_write(&ktable, 1, &nl, 1);

        /* Redisplay prompt */
        if (tty_prompt_len > 0) {
            xscore_fd_write(&ktable, 1, tty_prompt, tty_prompt_len);
        }
        /* Redisplay current line buffer */
        if (tty_line_len > 0) {
            xscore_fd_write(&ktable, 1, tty_line_buf, tty_line_len);
        }
        xscore_fd_table_close_all(&ktable);
    } else {
        /* First tab on ambiguous match: ring bell without adding spaces */
        struct xscore_fd_table ktable;
        xscore_fd_table_initialize(&ktable);
        char bell = '\a';
        xscore_fd_write(&ktable, 1, &bell, 1);
        xscore_fd_table_close_all(&ktable);
    }
}

static void tty_process_char(char c) {
    if (c != '\t') {
        tty_tab_count = 0;
    }

    /* Handle ANSI escape sequences from serial consoles (\033[A, \033OA, etc.) */
    if (tty_esc_state == 0) {
        if (c == '\033') {
            tty_esc_state = 1;
            return;
        }
    } else if (tty_esc_state == 1) {
        if (c == '[' || c == 'O') {
            tty_esc_state = 2;
            return;
        }
        tty_esc_state = 0;
        return;
    } else if (tty_esc_state == 2) {
        if (c == 'A') {
            tty_esc_state = 0;
            tty_handle_up_arrow();
            return;
        }
        if (c == 'B') {
            tty_esc_state = 0;
            tty_handle_down_arrow();
            return;
        }
        if (c >= '0' && c <= '9') {
            tty_esc_state = 3;
            return;
        }
        tty_esc_state = 0;
        return;
    } else if (tty_esc_state == 3) {
        if (c == '~' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            tty_esc_state = 0;
            return;
        }
        if ((c >= '0' && c <= '9') || c == ';') {
            return;
        }
        tty_esc_state = 0;
        return;
    }

    if (c == '\t' && (tty_termios.c_lflag & AKAOS_ICANON)) {
        tty_handle_tab();
        return;
    }

    if (c == '\r' && (tty_termios.c_iflag & AKAOS_ICRNL)) c = '\n';

    /* Serial consoles deliver control keys as bytes rather than PS/2
     * scancodes.  Handle the same line-discipline signals on every input
     * path so Ctrl+C/Ctrl+Z/Ctrl+D are reliable over UART and VirtIO too. */
    if (c == 3 && (tty_termios.c_lflag & AKAOS_ISIG)) {
        int foreground_signal =
            xscore_process_signal_foreground(AKAOS_SIGINT);
        tty_line_len = 0;
        tty_read_pos = 0;
        tty_history_index = -1;
        tty_line_ready = foreground_signal ? 0 : 1;
        if (!foreground_signal) {
            /* Deliver an actual empty line (not a zero-length read): Ctrl+C
             * at an idle prompt must redraw the shell, while Ctrl+D remains
             * the distinct canonical EOF indication. */
            tty_line_buf[0] = '\n';
            tty_line_len = 1;
        }

        struct xscore_fd_table ktable;
        xscore_fd_table_initialize(&ktable);
        const char interrupt_text[] = "^C\n";
        (void)xscore_fd_write(&ktable, 1, interrupt_text,
                              sizeof(interrupt_text) - 1);
        xscore_fd_table_close_all(&ktable);
        return;
    }
    if (c == 26 && (tty_termios.c_lflag & AKAOS_ISIG)) {
        int foreground_signal =
            xscore_process_signal_foreground(AKAOS_SIGTSTP);
        tty_line_len = 0;
        tty_read_pos = 0;
        tty_history_index = -1;
        tty_line_ready = foreground_signal ? 0 : 1;
        if (!foreground_signal) {
            tty_line_buf[0] = '\n';
            tty_line_len = 1;
        }

        struct xscore_fd_table ktable;
        xscore_fd_table_initialize(&ktable);
        const char stop_text[] = "^Z\n";
        (void)xscore_fd_write(&ktable, 1, stop_text,
                              sizeof(stop_text) - 1);
        xscore_fd_table_close_all(&ktable);
        return;
    }
    if (c == 4 && (tty_termios.c_lflag & AKAOS_ICANON)) {
        if (!tty_line_ready) {
            tty_line_ready = 1;
            tty_read_pos = 0;
        }
        return;
    }

    if ((tty_termios.c_lflag & AKAOS_ICANON) == 0) {
        if (tty_line_len < TTY_BUF_SIZE - 1) {
            tty_line_buf[tty_line_len++] = c;
            tty_line_ready = tty_line_len >=
                (tty_termios.c_cc[AKAOS_VMIN] ?
                 tty_termios.c_cc[AKAOS_VMIN] : 1);
            tty_read_pos = 0;
            if (tty_echo_enabled) {
                struct xscore_fd_table ktable;
                xscore_fd_table_initialize(&ktable);
                xscore_fd_write(&ktable, 1, &c, 1);
                xscore_fd_table_close_all(&ktable);
            }
        }
        return;
    }

    if (c == '\b' || c == 0x7f) {
        if (tty_line_len > 0) {
            char removed = tty_line_buf[--tty_line_len];
            if (tty_echo_enabled) {
                struct xscore_fd_table ktable;
                xscore_fd_table_initialize(&ktable);
                if (removed == '\t') {
                    size_t col = tty_visual_len(tty_line_buf, tty_line_len);
                    size_t spaces = 4 - (col % 4);
                    for (size_t s = 0; s < spaces; ++s) {
                        char b = '\b';
                        xscore_fd_write(&ktable, 1, &b, 1);
                    }
                } else {
                    char b = '\b';
                    xscore_fd_write(&ktable, 1, &b, 1);
                }
                xscore_fd_table_close_all(&ktable);
            }
        }
        return;
    }

    if (c == '\n' || c == '\r') {
        if (tty_echo_enabled && tty_line_len > 0) {
            /* Record non-empty command line in history */
            if (tty_history_count < TTY_HISTORY_MAX) {
                size_t copy_len = tty_line_len < TTY_BUF_SIZE - 1 ? tty_line_len : TTY_BUF_SIZE - 1;
                for (size_t i = 0; i < copy_len; ++i) {
                    tty_history[tty_history_count][i] = tty_line_buf[i];
                }
                tty_history[tty_history_count][copy_len] = '\0';
                ++tty_history_count;
            } else {
                for (int i = 0; i < TTY_HISTORY_MAX - 1; ++i) {
                    for (size_t j = 0; j < TTY_BUF_SIZE; ++j) {
                        tty_history[i][j] = tty_history[i + 1][j];
                    }
                }
                size_t copy_len = tty_line_len < TTY_BUF_SIZE - 1 ? tty_line_len : TTY_BUF_SIZE - 1;
                for (size_t i = 0; i < copy_len; ++i) {
                    tty_history[TTY_HISTORY_MAX - 1][i] = tty_line_buf[i];
                }
                tty_history[TTY_HISTORY_MAX - 1][copy_len] = '\0';
            }
        }
        tty_history_index = -1;

        if (tty_line_len < TTY_BUF_SIZE - 1) {
            tty_line_buf[tty_line_len++] = '\n';
        }
        tty_line_ready = 1;
        tty_read_pos = 0;
        /* Echo newline to console */
        struct xscore_fd_table ktable;
        xscore_fd_table_initialize(&ktable);
        char nl = '\n';
        xscore_fd_write(&ktable, 1, &nl, 1);
        xscore_fd_table_close_all(&ktable);
        return;
    }

    if (tty_line_len < TTY_BUF_SIZE - 2) {
        tty_line_buf[tty_line_len++] = c;
        if (tty_echo_enabled) {
            struct xscore_fd_table ktable;
            xscore_fd_table_initialize(&ktable);
            xscore_fd_write(&ktable, 1, &c, 1);
            xscore_fd_table_close_all(&ktable);
        }
    }
}

void xscore_tty_input_char(char c) {
    /* Publish each physical byte immediately for input-event consumers. */
    xscore_input_publish(AKAOS_EV_TEXT, (uint16_t)(uint8_t)c,
                         (int32_t)(uint8_t)c);
    if (xscore_input_is_grabbed()) return;

    /* Do not lose bytes that arrive while a previous canonical/raw read is
     * ready.  During a drain, IRQ input is queued as well so it cannot race
     * the line currently being assembled. */
    if (tty_line_ready || tty_draining_pending) {
        tty_pending_push(c);
        return;
    }
    tty_process_char(c);
}

void xscore_tty_input_scancode(uint8_t scan_code) {
    if (scan_code == 0xe0) {
        tty_extended = 1;
        return;
    }

    xscore_input_publish(AKAOS_EV_KEY, (uint16_t)(scan_code & 0x7f),
                         (scan_code & 0x80) ? 0 : 1);

    if (tty_extended) {
        tty_extended = 0;
        if (xscore_input_is_grabbed()) return;
        if (scan_code == 0x48) { /* Up Arrow (PS/2) */
            if (tty_line_ready || tty_draining_pending) {
                tty_pending_push('\033');
                tty_pending_push('[');
                tty_pending_push('A');
            } else {
                tty_handle_up_arrow();
            }
            return;
        } else if (scan_code == 0x50) { /* Down Arrow (PS/2) */
            if (tty_line_ready || tty_draining_pending) {
                tty_pending_push('\033');
                tty_pending_push('[');
                tty_pending_push('B');
            } else {
                tty_handle_down_arrow();
            }
            return;
        }
        return;
    }

    /* VirtIO Keyboard (Linux keycodes: KEY_UP=103, KEY_DOWN=108) */
    if (scan_code == 103) {
        if (xscore_input_is_grabbed()) return;
        if (tty_line_ready || tty_draining_pending) {
            tty_pending_push('\033');
            tty_pending_push('[');
            tty_pending_push('A');
        } else {
            tty_handle_up_arrow();
        }
        return;
    }
    if (scan_code == 108) {
        if (xscore_input_is_grabbed()) return;
        if (tty_line_ready || tty_draining_pending) {
            tty_pending_push('\033');
            tty_pending_push('[');
            tty_pending_push('B');
        } else {
            tty_handle_down_arrow();
        }
        return;
    }

    if (scan_code == 0x1d) {
        tty_ctrl = 1;
        return;
    }
    if (scan_code == 0x9d) {
        tty_ctrl = 0;
        return;
    }
    if (scan_code == 0x2a || scan_code == 0x36) {
        tty_shift = 1;
        return;
    }
    if (scan_code == 0xaa || scan_code == 0xb6) {
        tty_shift = 0;
        return;
    }
    if (scan_code & 0x80) return;

    if (scan_code == 0x1c) {
        xscore_tty_input_char('\n');
        return;
    }
    if (scan_code == 0x0e) {
        xscore_tty_input_char('\b');
        return;
    }
    if (scan_code == 0x0f) {
        xscore_tty_input_char('\t');
        return;
    }

    if (!xscore_input_is_grabbed() && tty_ctrl && scan_code == 0x2e &&
        (tty_termios.c_lflag & AKAOS_ISIG)) {
        int foreground_signal =
            xscore_process_signal_foreground(AKAOS_SIGINT);
        tty_line_len = 0;
        tty_read_pos = 0;
        tty_history_index = -1;
        tty_line_ready = foreground_signal ? 0 : 1;
        if (!foreground_signal) {
            tty_line_buf[0] = '\n';
            tty_line_len = 1;
        }

        /* Match a conventional TTY's visible interrupt indication. */
        struct xscore_fd_table ktable;
        xscore_fd_table_initialize(&ktable);
        const char interrupt_text[] = "^C\n";
        (void)xscore_fd_write(&ktable, 1, interrupt_text,
                              sizeof(interrupt_text) - 1);
        xscore_fd_table_close_all(&ktable);
        return;
    }

    if (!xscore_input_is_grabbed() && tty_ctrl && scan_code == 0x2c &&
        (tty_termios.c_lflag & AKAOS_ISIG)) {
        int foreground_signal =
            xscore_process_signal_foreground(AKAOS_SIGTSTP);
        tty_line_len = 0;
        tty_read_pos = 0;
        tty_history_index = -1;
        tty_line_ready = foreground_signal ? 0 : 1;
        if (!foreground_signal) {
            tty_line_buf[0] = '\n';
            tty_line_len = 1;
        }

        struct xscore_fd_table ktable;
        xscore_fd_table_initialize(&ktable);
        const char stop_text[] = "^Z\n";
        (void)xscore_fd_write(&ktable, 1, stop_text,
                              sizeof(stop_text) - 1);
        xscore_fd_table_close_all(&ktable);
        return;
    }

    /* Ctrl+D submits EOF without echoing a control character.  With an
     * empty canonical line this makes stdin return zero; with buffered text
     * it submits that text immediately, matching a terminal's EOF action. */
    if (tty_ctrl && scan_code == 0x20 &&
        (tty_termios.c_lflag & AKAOS_ICANON)) {
        if (!tty_line_ready) {
            tty_line_ready = 1;
            tty_read_pos = 0;
        }
        return;
    }

    char character = 0;
    if (scan_code < 128) {
        character = tty_shift ? tty_shift_keymap[scan_code] : tty_keymap[scan_code];
    }
    if (character) {
        xscore_tty_input_char(character);
    }
}

int xscore_tty_interrupts_enabled(void) { return tty_irq_enabled; }

int xscore_tty_enable_interrupts(void) {
#if defined(__x86_64__) || defined(__i386__)
    if (!xscore_irq_route_isa(1) || !xscore_irq_route_isa(4) ||
        !xscore_irq_route_isa(12)) return 0;
    uintptr_t saved = xscore_irq_save();
    uint8_t command;
    xscore_tty_poll_keyboard();
    if (!xscore_ps2_wait_input_clear()) goto failed;
    port_out8(0x64, 0x20);
    if (!xscore_ps2_wait_output(&command) || !xscore_ps2_wait_input_clear()) goto failed;
    port_out8(0x64, 0x60);
    if (!xscore_ps2_wait_input_clear()) goto failed;
    command = (uint8_t)((command | 3U) & (uint8_t)~0x30U);
    port_out8(0x60, command); /* IRQs on; keyboard and auxiliary clocks on. */
    port_out8(0x3f8 + 4, port_in8(0x3f8 + 4) | 8); /* OUT2 gates UART IRQ. */
    port_out8(0x3f8 + 1, 1); /* Receive data interrupt only. */
    tty_irq_enabled = 1;
    xscore_irq_restore(saved);
    return 1;
failed:
    xscore_irq_restore(saved);
#endif
    return 0;
}

void xscore_tty_poll_keyboard(void) {
    if (!tty_initialized) return;
    uintptr_t saved = xscore_irq_save();
#if defined(__x86_64__) || defined(__i386__)
    while (port_in8(0x64) & 1) {
        uint8_t status = port_in8(0x64);
        uint8_t sc = port_in8(0x60);
        if (status & 0x20) {
            ps2_mouse_input_byte(sc);
        } else {
            xscore_tty_input_scancode(sc);
        }
    }
    while (port_in8(0x3F8 + 5) & 1) {
        uint8_t ch = port_in8(0x3F8);
        if (ch == '\r') ch = '\n';
        xscore_tty_input_char((char)ch);
    }
#elif defined(__aarch64__)
    xscore_virtio_input_poll();
    /* QEMU virt PL011 UART */
    while (!(*(volatile uint32_t*)0x09000018 & (1 << 4))) {
        uint8_t ch = (uint8_t)(*(volatile uint32_t*)0x09000000 & 0xFF);
        if (ch == '\r') ch = '\n';
        xscore_tty_input_char((char)ch);
    }
#elif defined(__arm__)
    xscore_virtio_input_poll();
    /* QEMU virt PL011 UART */
    while (!(*(volatile uint32_t*)0x09000018 & (1 << 4))) {
        uint8_t ch = (uint8_t)(*(volatile uint32_t*)0x09000000 & 0xFF);
        if (ch == '\r') ch = '\n';
        xscore_tty_input_char((char)ch);
    }
#elif defined(__riscv)
    xscore_virtio_input_poll();
    /* QEMU virt NS16550A UART */
    while (*(volatile uint8_t*)0x10000005 & 1) {
        uint8_t ch = *(volatile uint8_t*)0x10000000;
        if (ch == '\r') ch = '\n';
        xscore_tty_input_char((char)ch);
    }
#endif
    xscore_irq_restore(saved);
}

int64_t xscore_tty_read(void *buf, uint64_t count) {
    if (!buf || count == 0) return 0;

    while (!tty_line_ready) {
        xscore_tty_poll_keyboard();
        if (tty_line_ready) break;
        if (xscore_process_signal_pending_current()) return -AKAOS_EINTR;
        xscore_thread_yield();
#if !defined(__x86_64__) && !defined(__i386__)
        if (!tty_line_ready) xscore_device_irq_wait();
#endif
    }

    char *dst = buf;
    uint64_t bytes_to_copy = 0;
    while (tty_read_pos < tty_line_len && bytes_to_copy < count) {
        dst[bytes_to_copy++] = tty_line_buf[tty_read_pos++];
    }

    if (tty_read_pos >= tty_line_len) {
        tty_line_len = 0;
        tty_read_pos = 0;
        tty_line_ready = 0;
        /* Move the oldest queued burst into the fresh line now that the
         * caller consumed the previous one.  If the queued bytes contain a
         * complete line, the next read will return it immediately. */
        tty_drain_pending();
    }

    return (int64_t)bytes_to_copy;
}
