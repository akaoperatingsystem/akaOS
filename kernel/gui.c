/* ============================================================
 * akaOS — GUI Desktop (Simplified, Robust)
 * ============================================================ */
#include "gui.h"
#include "fb.h"
#include "fs.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "shell.h"
#include "string.h"
#include "sysmon.h"
#include "time.h"

#define CHAR_W 8
#define CHAR_H 8
#define TASKBAR_H 30
#define TITLE_H 22
#define TERM_COLS 90
#define TERM_ROWS 40
#define TERM_PAD 4

/* Terminal text buffer */
static char tbuf[TERM_ROWS][TERM_COLS];
static uint32_t tfg[TERM_ROWS][TERM_COLS];
static int tcx = 0, tcy = 0;
static uint32_t tcur_fg = 0xa9b1d6;

/* DOOM state */
volatile int doom_running = 0;
void *doom_exit_jmp_buf[5];

static int doom_vis = 0;
static int doom_minimized = 0;
static int doom_maximized = 0;
static int doom_x = 100, doom_y = 100, doom_w = 640, doom_h = 400;
extern uint32_t *DG_ScreenBuffer;

/* Window state */
int active_window_focus =
    1; /* 0=Desktop, 1=Term, 2=About, 3=Sysmon, 4=Settings, 5=DOOM */
int wx, wy, ww, wh;
int win_vis = 0;
int about_vis = 0;
int sysmon_vis = 0;
int settings_vis = 0;

static int dragging = 0, dox = 0, doy = 0;
static int prev_btn = 0;

int suppress_printf = 0;

/* Shell input */
static char cmd[256];
static int clen = 0;

/* GUI mode flag */
volatile int gui_mode_active = 0;

/* Terminal output functions (called by VGA when gui_mode_active=1) */
static void tscroll(void) {
  for (int r = 0; r < TERM_ROWS - 1; r++) {
    memcpy(tbuf[r], tbuf[r + 1], TERM_COLS);
    memcpy(tfg[r], tfg[r + 1], TERM_COLS * 4);
  }
  memset(tbuf[TERM_ROWS - 1], 0, TERM_COLS);
  for (int c = 0; c < TERM_COLS; c++)
    tfg[TERM_ROWS - 1][c] = 0xa9b1d6;
}

void gui_term_putchar(char c) {
  if (c == '\n') {
    tcx = 0;
    tcy++;
    if (tcy >= TERM_ROWS) {
      tscroll();
      tcy = TERM_ROWS - 1;
    }
    return;
  }
  if (c == '\r') {
    tcx = 0;
    return;
  }
  if (c == '\b') {
    if (tcx > 0) {
      tcx--;
      tbuf[tcy][tcx] = ' ';
    }
    return;
  }
  if (c == '\t') {
    int s = 4 - (tcx % 4);
    for (int i = 0; i < s; i++)
      gui_term_putchar(' ');
    return;
  }
  if (tcx >= TERM_COLS) {
    tcx = 0;
    tcy++;
    if (tcy >= TERM_ROWS) {
      tscroll();
      tcy = TERM_ROWS - 1;
    }
  }
  if (tcy >= 0 && tcy < TERM_ROWS && tcx >= 0 && tcx < TERM_COLS) {
    tbuf[tcy][tcx] = c;
    tfg[tcy][tcx] = tcur_fg;
  }
  tcx++;
}

void gui_term_print(const char *s) {
  while (*s)
    gui_term_putchar(*s++);
}
void gui_term_set_color(uint32_t fg) { tcur_fg = fg; }

/* ============================================================
 * Drawing helpers
 * ============================================================ */
static void draw_desktop(void) {
  /* Solid dark background (safe, no division) */
  fb_fill_rect(0, 0, (int)fb_width(), (int)fb_height() - TASKBAR_H, 0x0d1117);
}

static void draw_taskbar(void) {
  int w = (int)fb_width(), h = (int)fb_height();
  int ty = h - TASKBAR_H;
  fb_fill_rect(0, ty, w, TASKBAR_H, 0x1e1e2e);
  fb_draw_hline(0, ty, w, 0x30363d);

  /* akaOS button */
  fb_fill_rect(4, ty + 4, 70, TASKBAR_H - 8, 0x3d3d5c);
  fb_draw_string(10, ty + 11, "akaOS", 0x7dcfff, 0x3d3d5c, 1);

  /* Terminal button */
  uint32_t tbc = win_vis ? 0x4a4a6a : 0x2d2d44;
  fb_fill_rect(80, ty + 4, 80, TASKBAR_H - 8, tbc);
  fb_draw_string(88, ty + 11, "Terminal", 0xFFFFFF, tbc, 1);

  /* DOOM button */
  if (doom_running) {
    uint32_t dbc = (doom_vis && !doom_minimized) ? 0x4a4a6a : 0x2d2d44;
    fb_fill_rect(170, ty + 4, 80, TASKBAR_H - 8, dbc);
    fb_draw_string(178, ty + 11, "DOOM", 0xFFFFFF, dbc, 1);
  }

  /* Clock */
  char up[32];
  timer_format_uptime(up, 32);
  int cl = (int)strlen(up);
  fb_draw_string(w - cl * CHAR_W - 8, ty + 11, up, 0xAAAAAA, 0x1e1e2e, 1);
}

static void draw_window(void) {
  if (!win_vis)
    return;

  /* Shadow */
  fb_fill_rect(wx + 3, wy + 3, ww, wh, 0x080808);

  /* Title bar */
  fb_fill_rect(wx, wy, ww, TITLE_H, 0x4a4a6a);
  fb_draw_string(wx + 8, wy + 7, "Terminal", 0xFFFFFF, 0x4a4a6a, 1);

  /* Close button (X) */
  fb_fill_rect(wx + ww - 20, wy + 3, 16, 16, 0xf7768e);
  fb_draw_char(wx + ww - 18, wy + 7, 'x', 0xFFFFFF, 0xf7768e, 1);

  /* Terminal body */
  int bx = wx, by = wy + TITLE_H;
  int bw = ww, bh = wh - TITLE_H;
  fb_fill_rect(bx, by, bw, bh, 0x1a1b26);

  /* Render text */
  for (int r = 0; r < TERM_ROWS; r++) {
    for (int c = 0; c < TERM_COLS; c++) {
      char ch = tbuf[r][c];
      if (ch >= 32 && ch <= 126) {
        fb_draw_char_nobg(bx + TERM_PAD + c * CHAR_W,
                          by + TERM_PAD + r * CHAR_H, ch, tfg[r][c], 1);
      }
    }
  }

  /* Cursor blink */
  if ((timer_get_ticks() / 50) % 2 == 0) {
    fb_fill_rect(bx + TERM_PAD + tcx * CHAR_W, by + TERM_PAD + tcy * CHAR_H,
                 CHAR_W, CHAR_H, 0xa9b1d6);
  }
}

static void draw_doom_window(void) {
  if (!doom_vis || doom_minimized || !DG_ScreenBuffer)
    return;

  int dw = doom_maximized ? (int)fb_width() : doom_w;
  int dh = doom_maximized ? ((int)fb_height() - TASKBAR_H - TITLE_H) : doom_h;
  int dx = doom_maximized ? 0 : doom_x;
  int dy = doom_maximized ? TITLE_H : doom_y + TITLE_H;
  int bx = doom_maximized ? 0 : doom_x;
  int by = doom_maximized ? 0 : doom_y;

  /* Shadow */
  if (!doom_maximized)
    fb_fill_rect(bx + 3, by + 3, dw, dh + TITLE_H, 0x080808);

  /* Background */
  fb_fill_rect(bx, by, dw, dh + TITLE_H, 0x000000);

  /* Title bar */
  fb_fill_rect(bx, by, dw, TITLE_H, 0x4a4a6a);
  fb_draw_string(bx + 8, by + 7, "DOOM", 0xFFFFFF, 0x4a4a6a, 1);

  /* Close button (X) */
  fb_fill_rect(bx + dw - 20, by + 3, 16, 16, 0xf7768e);
  fb_draw_char(bx + dw - 18, by + 7, 'x', 0xFFFFFF, 0xf7768e, 1);

  /* Maximize button (+) */
  fb_fill_rect(bx + dw - 40, by + 3, 16, 16, 0x9ece6a);
  fb_draw_char(bx + dw - 38, by + 7, '+', 0xFFFFFF, 0x9ece6a, 1);

  /* Minimize button (-) */
  fb_fill_rect(bx + dw - 60, by + 3, 16, 16, 0xe0af68);
  fb_draw_char(bx + dw - 58, by + 7, '-', 0xFFFFFF, 0xe0af68, 1);

  /* Render DOOM buffer (scaling nearest-neighbor) */
  int src_w = 640, src_h = 400;
  uint32_t *bb = fb_get_backbuffer();
  int bbw = (int)fb_width(), bbh = (int)fb_height();

  if (dw == src_w && dh == src_h && dx >= 0 && dy >= 0 && dx + dw <= bbw &&
      dy + dh <= bbh) {
    /* Fast path: direct copy via memcpy */
    for (int y = 0; y < dh; y++) {
      memcpy(&bb[(dy + y) * bbw + dx], &DG_ScreenBuffer[y * src_w], src_w * 4);
    }
  } else {
    /* Slow path: scaling (maximized) */
    for (int y = 0; y < dh; y++) {
      int sy = y * src_h / dh;
      if (sy >= src_h)
        sy = src_h - 1;
      uint32_t *dst_row = &bb[(dy + y) * bbw + dx];
      uint32_t *src_row = &DG_ScreenBuffer[sy * src_w];
      for (int x = 0; x < dw; x++) {
        int sx = x * src_w / dw;
        if (sx >= src_w)
          sx = src_w - 1;
        if (dx + x >= 0 && dx + x < bbw)
          dst_row[x] = src_row[sx];
      }
    }
  }
}

static void draw_cursor(int mx, int my) {
  /* Simple crosshair cursor (safer than bitmap) */
  for (int i = -5; i <= 5; i++) {
    fb_put_pixel(mx + i, my, 0xFFFFFF);
    fb_put_pixel(mx, my + i, 0xFFFFFF);
  }
}

void gui_force_redraw(void) {
  fb_fill_rect(0, 0, (int)fb_width(), (int)fb_height(), 0x000000);
  void draw_desktop(void);
  void draw_taskbar(void);
  draw_desktop();
  win_vis = 1; /* Force terminal window to be visible to show the error */
  draw_window();
  if (doom_running)
    draw_doom_window();
  draw_taskbar();
  fb_flip();
}

/* ============================================================
 * Input handling
 * ============================================================ */
static void handle_key(char c) {
  if (c == '\n') {
    cmd[clen] = '\0';
    gui_term_putchar('\n');
    if (clen > 0)
      shell_execute_one(cmd);
    clen = 0;
    shell_print_prompt();
    return;
  }
  if (c == '\b') {
    if (clen > 0) {
      clen--;
      gui_term_putchar('\b');
    }
    return;
  }
  if ((unsigned char)c >= 0x80)
    return;
  if (c >= 32 && c <= 126 && clen < 254) {
    cmd[clen++] = c;
    gui_term_putchar(c);
  }
}

/* ============================================================
 * About Window
 * ============================================================ */
static int ax, ay, aw = 450, ah = 200;

extern uint32_t sys_total_memory_mb;

static void get_cpu_string(char *buf) {
  uint32_t m_eax;
  asm volatile("cpuid" : "=a"(m_eax) : "a"(0x80000000) : "ebx", "ecx", "edx");
  uint32_t *ptr = (uint32_t *)buf;
  if (m_eax >= 0x80000004) {
    asm volatile("cpuid"
                 : "=a"(ptr[0]), "=b"(ptr[1]), "=c"(ptr[2]), "=d"(ptr[3])
                 : "a"(0x80000002));
    asm volatile("cpuid"
                 : "=a"(ptr[4]), "=b"(ptr[5]), "=c"(ptr[6]), "=d"(ptr[7])
                 : "a"(0x80000003));
    asm volatile("cpuid"
                 : "=a"(ptr[8]), "=b"(ptr[9]), "=c"(ptr[10]), "=d"(ptr[11])
                 : "a"(0x80000004));
    buf[48] = '\0';
  } else {
    asm volatile("cpuid" : "=b"(ptr[0]), "=d"(ptr[1]), "=c"(ptr[2]) : "a"(0));
    buf[12] = '\0';
  }
}

static void draw_about_window(void) {
  if (!about_vis)
    return;

  /* Shadow */
  fb_fill_rect(ax + 5, ay + 5, aw, ah, 0x050505);

  /* Main body (macOS-like rounded/sleek feel, though we use rects) */
  fb_fill_rect(ax, ay, aw, ah, 0x1e1e2e);
  fb_draw_rect(ax, ay, aw, ah, 0x565f89);

  /* Title bar */
  fb_fill_rect(ax, ay, aw, TITLE_H, 0x2a2a4a);

  /* Close button (macOS red circle style, we do a red square) */
  fb_fill_rect(ax + 8, ay + 4, 14, 14, 0xf7768e);
  fb_draw_rect(ax + 8, ay + 4, 14, 14, 0xbb5566);

  /* Top Title */
  fb_draw_string(ax + aw / 2 - (5 * 8), ay + 40, "akaOS", 0x7dcfff, 0x1e1e2e,
                 2); /* Scaled up */
  fb_draw_string(ax + 20, ay + 80, "Version:     1.0 (x86_64)", 0xAAAAAA,
                 0x1e1e2e, 1);

  /* Real Specs */
  char cpu_str[64];
  memset(cpu_str, 0, sizeof(cpu_str));
  get_cpu_string(cpu_str);
  fb_draw_string(ax + 20, ay + 100, "Processor:   ", 0xAAAAAA, 0x1e1e2e, 1);
  fb_draw_string(ax + 124, ay + 100, cpu_str, 0xFFFFFF, 0x1e1e2e, 1);

  char mem_str[32];
  int_to_str(sys_total_memory_mb, mem_str);
  strcat(mem_str, " MB RAM");
  fb_draw_string(ax + 20, ay + 120, "Memory:      ", 0xAAAAAA, 0x1e1e2e, 1);
  fb_draw_string(ax + 124, ay + 120, mem_str, 0xFFFFFF, 0x1e1e2e, 1);

  fb_draw_string(ax + 20, ay + 140, "Display:     Multiboot2 Framebuffer",
                 0xAAAAAA, 0x1e1e2e, 1);
  fb_draw_string(ax + 20, ay + 180, "Developed by akaOS Team", 0xbb9af7,
                 0x1e1e2e, 1);
}

/* ============================================================
 * System Monitor (Htop-style Window)
 * ============================================================ */

static int sx, sy, sw = 480, sh = 340;

/* ============================================================
 * Settings Window
 * ============================================================ */
static int settx, setty, settw = 460, setth = 300;
static int settings_tab = 0; /* 0 = Display, 1 = Desktop, 2 = About */
#define SETTINGS_SIDEBAR_W 120
#define SETTINGS_TAB_COUNT 4

/* Display settings option indices */
static int disp_res_idx = 2;   /* default 1024x768 */
static int disp_scale_idx = 0; /* default 1x */
static int disp_hz_idx = 0;    /* default 60 Hz */
static int disp_depth_idx = 2; /* default 32-bit */

/* Presets */
static const int disp_res_w[] = {640, 800, 1024, 1280, 1920};
static const int disp_res_h[] = {480, 600, 768, 720, 1080};
#define DISP_RES_COUNT 5
static const char *disp_scale_names[] = {"1x (100%)", "1.25x (125%)",
                                         "1.5x (150%)"};
#define DISP_SCALE_COUNT 3
static const int disp_hz_vals[] = {60, 75, 120, 144};
#define DISP_HZ_COUNT 4
static const char *disp_depth_names[] = {"16-bit RGB565", "24-bit RGB",
                                         "32-bit ARGB"};
#define DISP_DEPTH_COUNT 3

/* Y offsets of each control row relative to content area top — used for
 * hit-testing */
#define DISP_ROW_RES 32
#define DISP_ROW_SCALE 94
#define DISP_ROW_HZ 156
#define DISP_ROW_DEPTH 218
#define DISP_ROW_H 18
#define DISP_CTRL_X 130
#define DISP_ARROW_W 14

static void draw_bar_graph(int x, int y, int width, int percent, uint32_t bg,
                           uint32_t fg) {
  fb_fill_rect(x, y, width, 12, bg);
  int fill_w = (width * percent) / 100;
  if (fill_w > 0) {
    /* Color gradient based on usage */
    uint32_t c = fg;
    if (percent > 60)
      c = 0xe0af68; /* Yellow */
    if (percent > 85)
      c = 0xf7768e; /* Red */
    fb_fill_rect(x, y, fill_w, 12, c);
  }

  char pct_str[16];
  int_to_str(percent, pct_str);
  strcat(pct_str, "%");
  fb_draw_string(x + width + 8, y + 2, pct_str, 0xAAAAAA, 0x1a1b26, 1);
}

static void draw_sysmon_window(void) {
  if (!sysmon_vis)
    return;

  /* Shadow */
  fb_fill_rect(sx + 3, sy + 3, sw, sh, 0x080808);

  /* Background (htop style dark) */
  fb_fill_rect(sx, sy, sw, sh, 0x1a1b26);
  fb_draw_rect(sx, sy, sw, sh, 0x3d3d5c);

  /* Title bar */
  fb_fill_rect(sx, sy, sw, TITLE_H, 0x3d3d5c);
  fb_draw_string(sx + 6, sy + 7, "System Monitor", 0xFFFFFF, 0x3d3d5c, 1);

  /* Close Button */
  fb_fill_rect(sx + sw - 20, sy + 4, 16, 16, 0xf7768e);
  fb_draw_char(sx + sw - 16, sy + 8, 'X', 0xFFFFFF, 0xf7768e, 1);

  /* Get Metrics */
  struct sys_metrics m;
  sysmon_update(&m);

  /* --- Top Section: Graphs --- */
  int gy = sy + TITLE_H + 10;
  int gx = sx + 10;

  for (int i = 0; i < m.core_count; i++) {
    char c_str[4];
    int_to_str(i, c_str);
    fb_draw_string(gx, gy, c_str, 0x7dcfff, 0x1a1b26, 1);
    fb_draw_char(gx + 8, gy, '[', 0xAAAAAA, 0x1a1b26, 1);
    draw_bar_graph(gx + 20, gy, 150, m.cpu_load_percent[i], 0x24283b, 0x9ece6a);
    fb_draw_char(gx + 175, gy, ']', 0xAAAAAA, 0x1a1b26, 1);
    gy += 16;
  }

  gy += 8;
  int mem_pct = m.mem_total_kb > 0 ? (m.mem_used_kb * 100) / m.mem_total_kb : 0;
  fb_draw_string(gx, gy, "Mem[", 0xAAAAAA, 0x1a1b26, 1);
  draw_bar_graph(gx + 36, gy, 134, mem_pct, 0x24283b, 0xbb9af7);
  fb_draw_char(gx + 175, gy, ']', 0xAAAAAA, 0x1a1b26, 1);

  char m_str[64];
  int_to_str(m.mem_used_kb / 1024, m_str);
  strcat(m_str, "M / ");
  char m2_str[16];
  int_to_str(m.mem_total_kb / 1024, m2_str);
  strcat(m_str, m2_str);
  strcat(m_str, "M");
  fb_draw_string(gx, gy + 16, m_str, 0xAAAAAA, 0x1a1b26, 1);

  /* Right Stats */
  char u_str[32];
  timer_format_uptime(u_str, 32);
  fb_draw_string(sx + sw - 140, sy + TITLE_H + 10, "Uptime:", 0xAAAAAA,
                 0x1a1b26, 1);
  fb_draw_string(sx + sw - 80, sy + TITLE_H + 10, u_str, 0xFFFFFF, 0x1a1b26, 1);

  fb_draw_string(sx + sw - 140, sy + TITLE_H + 30, "Tasks:", 0xAAAAAA, 0x1a1b26,
                 1);
  char t_str[8];
  int_to_str(m.proc_count, t_str);
  fb_draw_string(sx + sw - 80, sy + TITLE_H + 30, t_str, 0xFFFFFF, 0x1a1b26, 1);

  /* --- Bottom Section: Process List --- */
  int py = gy + 40;

  /* Header */
  fb_fill_rect(sx + 4, py, sw - 8, 16, 0xbb9af7);
  fb_draw_string(sx + 10, py + 4,
                 "PID USER      PRI  S  CPU% MEM%   VIRT  COMMAND", 0x1a1b26,
                 0xbb9af7, 1);
  py += 20;

  for (int i = 0; i < m.proc_count; i++) {
    struct sys_proc *p = &m.procs[i];

    char pid_str[8];
    int_to_str(p->pid, pid_str);
    char cpu_s[8];
    int_to_str(p->cpu_percent, cpu_s);
    int mp = m.mem_total_kb > 0 ? (p->mem_kb * 100) / m.mem_total_kb : 0;
    char mem_s[8];
    int_to_str(mp, mem_s);
    char v_s[16];
    int_to_str(p->mem_kb, v_s);
    strcat(v_s, "K");

    fb_draw_string(sx + 10, py, pid_str, 0xFFFFFF, 0x1a1b26, 1);
    fb_draw_string(sx + 42, py, p->user, 0x7dcfff, 0x1a1b26, 1);
    fb_draw_string(sx + 122, py, "20", 0xAAAAAA, 0x1a1b26, 1); /* PRI */
    fb_draw_string(sx + 162, py, p->state,
                   (p->state[0] == 'R') ? 0x9ece6a : 0xAAAAAA, 0x1a1b26, 1);
    fb_draw_string(sx + 185, py, cpu_s, 0xFFFFFF, 0x1a1b26, 1);
    fb_draw_string(sx + 233, py, mem_s, 0xFFFFFF, 0x1a1b26, 1);
    fb_draw_string(sx + 280, py, v_s, 0xAAAAAA, 0x1a1b26, 1);
    fb_draw_string(sx + 340, py, p->name, 0xFFFFFF, 0x1a1b26, 1);
    py += 14;
  }
}

/* ============================================================
 * Settings Window
 * ============================================================ */
static const char *settings_tab_names[] = {"Display", "System", "Network",
                                           "About"};
static const uint32_t settings_tab_icons[] = {0x7dcfff, 0x9ece6a, 0xe0af68,
                                              0xbb9af7};

/* Draw a selector control:  label   [< value >] */
static void draw_selector(int cx, int cy, const char *label, const char *value,
                          int idx, int count) {
  fb_draw_string(cx + 20, cy, label, 0xAAAAAA, 0x1a1b26, 1);
  int bx = cx + DISP_CTRL_X;
  /* Left arrow */
  uint32_t la_bg = (idx > 0) ? 0x3d3d5c : 0x24283b;
  uint32_t la_fg = (idx > 0) ? 0xFFFFFF : 0x565f89;
  fb_fill_rect(bx, cy - 2, DISP_ARROW_W, DISP_ROW_H, la_bg);
  fb_draw_char(bx + 3, cy + 1, '<', la_fg, la_bg, 1);
  /* Value */
  fb_fill_rect(bx + DISP_ARROW_W, cy - 2, 160, DISP_ROW_H, 0x24283b);
  fb_draw_string(bx + DISP_ARROW_W + 8, cy, value, 0xFFFFFF, 0x24283b, 1);
  /* Right arrow */
  int rx = bx + DISP_ARROW_W + 160;
  uint32_t ra_bg = (idx < count - 1) ? 0x3d3d5c : 0x24283b;
  uint32_t ra_fg = (idx < count - 1) ? 0xFFFFFF : 0x565f89;
  fb_fill_rect(rx, cy - 2, DISP_ARROW_W, DISP_ROW_H, ra_bg);
  fb_draw_char(rx + 3, cy + 1, '>', ra_fg, ra_bg, 1);
}

static void draw_settings_tab_display(int cx, int cy, int cw, int ch) {
  /* Section: Resolution */
  fb_draw_string(cx + 10, cy + 10, "Resolution", 0x7dcfff, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 22, cw - 20, 0x30363d);
  char res_str[32];
  int_to_str(disp_res_w[disp_res_idx], res_str);
  strcat(res_str, " x ");
  char h_str[16];
  int_to_str(disp_res_h[disp_res_idx], h_str);
  strcat(res_str, h_str);
  draw_selector(cx, cy + DISP_ROW_RES, "Resolution:", res_str, disp_res_idx,
                DISP_RES_COUNT);
  /* Aspect ratio (derived) */
  int rw = disp_res_w[disp_res_idx], rh = disp_res_h[disp_res_idx];
  const char *aspect = "Custom";
  if (rw * 3 == rh * 4)
    aspect = "4:3";
  else if (rw * 9 == rh * 16)
    aspect = "16:9";
  else if (rw * 10 == rh * 16)
    aspect = "16:10";
  fb_draw_string(cx + 20, cy + 52, "Aspect Ratio:  ", 0x565f89, 0x1a1b26, 1);
  fb_draw_string(cx + 140, cy + 52, aspect, 0x7dcfff, 0x1a1b26, 1);

  /* Section: Scale */
  fb_draw_string(cx + 10, cy + 72, "Scale", 0x7dcfff, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 84, cw - 20, 0x30363d);
  draw_selector(cx, cy + DISP_ROW_SCALE,
                "UI Scale:", disp_scale_names[disp_scale_idx], disp_scale_idx,
                DISP_SCALE_COUNT);
  fb_draw_string(cx + 20, cy + 114, "Font Size:     8x8 bitmap", 0x565f89,
                 0x1a1b26, 1);

  /* Section: Refresh Rate */
  fb_draw_string(cx + 10, cy + 134, "Refresh Rate", 0x7dcfff, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 146, cw - 20, 0x30363d);
  char hz_str[16];
  int_to_str(disp_hz_vals[disp_hz_idx], hz_str);
  strcat(hz_str, " Hz");
  draw_selector(cx, cy + DISP_ROW_HZ, "Rate:", hz_str, disp_hz_idx,
                DISP_HZ_COUNT);
  fb_draw_string(cx + 20, cy + 176, "V-Sync:        Enabled", 0x565f89,
                 0x1a1b26, 1);

  /* Section: Color Depth */
  fb_draw_string(cx + 10, cy + 196, "Color Depth", 0x7dcfff, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 208, cw - 20, 0x30363d);
  draw_selector(cx, cy + DISP_ROW_DEPTH,
                "Bit Depth:", disp_depth_names[disp_depth_idx], disp_depth_idx,
                DISP_DEPTH_COUNT);

  /* Color preview swatches */
  int swx = cx + 20, swy = cy + 250;
  uint32_t swatches[] = {0xf7768e, 0xe0af68, 0x9ece6a,
                         0x7dcfff, 0xbb9af7, 0x7aa2f7};
  for (int i = 0; i < 6; i++)
    fb_fill_rect(swx + i * 22, swy, 18, 12, swatches[i]);
  fb_draw_string(cx + 20, swy + 16, "Theme palette preview", 0x565f89, 0x1a1b26,
                 1);
  /* Apply button */
  int apply_bx = cx + cw - 120, apply_by = cy + ch - 30;
  fb_fill_rect(apply_bx, apply_by, 100, 22, 0x3d3d5c);
  fb_draw_rect(apply_bx, apply_by, 100, 22, 0x565f89);
  fb_draw_string(apply_bx + 30, apply_by + 7, "Apply", 0x9ece6a, 0x3d3d5c, 1);
}

static void draw_settings_tab_system(int cx, int cy, int cw, int ch) {
  /* CPU Info */
  fb_draw_string(cx + 10, cy + 10, "Processor", 0x9ece6a, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 22, cw - 20, 0x30363d);

  char cpu_str[64];
  memset(cpu_str, 0, sizeof(cpu_str));
  get_cpu_string(cpu_str);
  fb_draw_string(cx + 20, cy + 32, cpu_str, 0xFFFFFF, 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 46, "Architecture:  x86_64", 0xAAAAAA, 0x1a1b26,
                 1);

  /* Memory */
  fb_draw_string(cx + 10, cy + 72, "Memory", 0x9ece6a, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 84, cw - 20, 0x30363d);

  char mem_str[32];
  int_to_str(sys_total_memory_mb, mem_str);
  strcat(mem_str, " MB Total RAM");
  fb_draw_string(cx + 20, cy + 94, mem_str, 0xFFFFFF, 0x1a1b26, 1);

  /* Memory bar */
  struct sys_metrics m;
  sysmon_update(&m);
  int mem_pct = m.mem_total_kb > 0 ? (m.mem_used_kb * 100) / m.mem_total_kb : 0;
  fb_fill_rect(cx + 20, cy + 112, cw - 50, 14, 0x24283b);
  int fill_w = ((cw - 50) * mem_pct) / 100;
  uint32_t bar_color = 0x9ece6a;
  if (mem_pct > 60)
    bar_color = 0xe0af68;
  if (mem_pct > 85)
    bar_color = 0xf7768e;
  if (fill_w > 0)
    fb_fill_rect(cx + 20, cy + 112, fill_w, 14, bar_color);
  char pct_str[16];
  int_to_str(mem_pct, pct_str);
  strcat(pct_str, "% used");
  fb_draw_string(cx + 20, cy + 130, pct_str, 0xAAAAAA, 0x1a1b26, 1);

  /* Uptime */
  fb_draw_string(cx + 10, cy + 156, "Uptime", 0x9ece6a, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 168, cw - 20, 0x30363d);
  char up[32];
  timer_format_uptime(up, 32);
  fb_draw_string(cx + 20, cy + 178, up, 0xFFFFFF, 0x1a1b26, 1);

  /* Kernel */
  fb_draw_string(cx + 10, cy + 204, "Kernel", 0x9ece6a, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 216, cw - 20, 0x30363d);
  fb_draw_string(cx + 20, cy + 226, "akaOS Kernel 1.0", 0xFFFFFF, 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 240, "Boot: Limine v10.x (x86_64)", 0xAAAAAA,
                 0x1a1b26, 1);
}

static void draw_settings_tab_network(int cx, int cy, int cw, int ch) {
  fb_draw_string(cx + 10, cy + 10, "Network Status", 0xe0af68, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 22, cw - 20, 0x30363d);

  /* Status indicator */
  fb_fill_rect(cx + 20, cy + 36, 10, 10, 0x9ece6a);
  fb_draw_string(cx + 36, cy + 36, "Connected", 0xFFFFFF, 0x1a1b26, 1);

  fb_draw_string(cx + 20, cy + 56, "Adapter:       e1000 (Intel)", 0xAAAAAA,
                 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 70, "Type:          Ethernet", 0xAAAAAA,
                 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 84, "IP Address:    10.0.2.15", 0xFFFFFF,
                 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 98, "Subnet Mask:   255.255.255.0", 0xAAAAAA,
                 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 112, "Gateway:       10.0.2.2", 0xAAAAAA,
                 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 126, "DNS:           10.0.2.3", 0xAAAAAA,
                 0x1a1b26, 1);

  fb_draw_string(cx + 10, cy + 156, "Statistics", 0xe0af68, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 168, cw - 20, 0x30363d);
  fb_draw_string(cx + 20, cy + 178, "Packets TX:    0", 0xAAAAAA, 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 192, "Packets RX:    0", 0xAAAAAA, 0x1a1b26, 1);
}

static void draw_settings_tab_about(int cx, int cy, int cw, int ch) {
  /* OS Name big */
  fb_draw_string(cx + (cw - 5 * 16) / 2, cy + 20, "akaOS", 0x7dcfff, 0x1a1b26,
                 2);
  fb_draw_string(cx + (cw - 20 * 8) / 2, cy + 50, "A Modern x86_64 Kernel",
                 0x565f89, 0x1a1b26, 1);

  fb_draw_hline(cx + 20, cy + 70, cw - 40, 0x30363d);

  fb_draw_string(cx + 20, cy + 86, "Version:       1.0.0", 0xAAAAAA, 0x1a1b26,
                 1);
  fb_draw_string(cx + 20, cy + 100, "Build:         x86_64-elf", 0xAAAAAA,
                 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 114, "Bootloader:    Limine v10.x", 0xAAAAAA,
                 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 128, "Graphics:      Linear Framebuffer",
                 0xAAAAAA, 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 142, "Shell:         Built-in POSIX", 0xAAAAAA,
                 0x1a1b26, 1);

  fb_draw_hline(cx + 20, cy + 162, cw - 40, 0x30363d);

  fb_draw_string(cx + 20, cy + 178, "Developed with passion", 0xbb9af7,
                 0x1a1b26, 1);
  fb_draw_string(cx + 20, cy + 198, "github.com/akaoperatingsystem/akaOS",
                 0x7aa2f7, 0x1a1b26, 1);

  /* License badge */
  fb_fill_rect(cx + 20, cy + 224, 80, 18, 0x3d3d5c);
  fb_draw_string(cx + 26, cy + 229, "MIT License", 0x9ece6a, 0x3d3d5c, 1);
}

static void draw_settings_window(void) {
  if (!settings_vis)
    return;

  /* Shadow */
  fb_fill_rect(settx + 4, setty + 4, settw, setth, 0x050505);

  /* Background */
  fb_fill_rect(settx, setty, settw, setth, 0x1a1b26);
  fb_draw_rect(settx, setty, settw, setth, 0x3d3d5c);

  /* Title bar */
  fb_fill_rect(settx, setty, settw, TITLE_H, 0x2a2a4a);
  fb_draw_string(settx + 8, setty + 7, "Settings", 0xFFFFFF, 0x2a2a4a, 1);

  /* Close button */
  fb_fill_rect(settx + settw - 20, setty + 3, 16, 16, 0xf7768e);
  fb_draw_char(settx + settw - 16, setty + 7, 'X', 0xFFFFFF, 0xf7768e, 1);

  /* Sidebar */
  int sidebar_x = settx;
  int sidebar_y = setty + TITLE_H;
  int sidebar_h = setth - TITLE_H;
  fb_fill_rect(sidebar_x, sidebar_y, SETTINGS_SIDEBAR_W, sidebar_h, 0x16161e);
  fb_draw_hline(sidebar_x + SETTINGS_SIDEBAR_W, sidebar_y, 1,
                0x30363d); /* divider */
  for (int j = sidebar_y; j < sidebar_y + sidebar_h; j++)
    fb_put_pixel(sidebar_x + SETTINGS_SIDEBAR_W, j, 0x30363d);

  /* Tab buttons */
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    int ty = sidebar_y + 8 + i * 28;
    uint32_t bg = (i == settings_tab) ? 0x2a2a4a : 0x16161e;
    uint32_t fg = (i == settings_tab) ? settings_tab_icons[i] : 0x565f89;
    fb_fill_rect(sidebar_x + 4, ty, SETTINGS_SIDEBAR_W - 8, 24, bg);
    if (i == settings_tab) {
      /* Active indicator bar */
      fb_fill_rect(sidebar_x + 2, ty, 3, 24, settings_tab_icons[i]);
    }
    fb_draw_string(sidebar_x + 14, ty + 8, settings_tab_names[i], fg, bg, 1);
  }

  /* Content area */
  int cx = settx + SETTINGS_SIDEBAR_W + 1;
  int cy = setty + TITLE_H;
  int cw = settw - SETTINGS_SIDEBAR_W - 1;
  int ch = setth - TITLE_H;

  switch (settings_tab) {
  case 0:
    draw_settings_tab_display(cx, cy, cw, ch);
    break;
  case 1:
    draw_settings_tab_system(cx, cy, cw, ch);
    break;
  case 2:
    draw_settings_tab_network(cx, cy, cw, ch);
    break;
  case 3:
    draw_settings_tab_about(cx, cy, cw, ch);
    break;
  }
}

/* ============================================================
 * Start Menu
 * ============================================================ */
static int start_menu_vis = 0;
static int sys_submenu_open = 0;
static int games_submenu_open = 0;
#define MENU_W 160
#define MENU_BASE_H                                                            \
  100 /* Base height: title (32) + System (20) + Games (20) + Reboot (28) */
#define SUBMENU_H 80       /* System: 4 items * 20px */
#define GAMES_SUBMENU_H 20 /* Games: 1 item * 20px */

static int get_menu_h(void) {
  return MENU_BASE_H + (sys_submenu_open ? SUBMENU_H : 0) +
         (games_submenu_open ? GAMES_SUBMENU_H : 0);
}

static void draw_start_menu(void) {
  if (!start_menu_vis)
    return;
  int h = (int)fb_height();
  int mh = get_menu_h();
  int mx = 4;
  int my = h - TASKBAR_H - mh - 4;

  /* Shadow */
  fb_fill_rect(mx + 4, my + 4, MENU_W, mh, 0x0a0a0a);

  /* Menu background */
  fb_fill_rect(mx, my, MENU_W, mh, 0x1e1e2e);
  fb_draw_rect(mx, my, MENU_W, mh, 0x3d3d5c);

  /* Title */
  fb_fill_rect(mx + 1, my + 1, MENU_W - 2, 24, 0x3d3d5c);
  fb_draw_string(mx + 8, my + 9, "akaOS Menu", 0xFFFFFF, 0x3d3d5c, 1);

  /* "System >" or "System v" category */
  int iy = my + 32;
  fb_draw_string(mx + 10, iy, sys_submenu_open ? "v System" : "> System",
                 0x7dcfff, 0x1e1e2e, 1);
  iy += 20;

  /* Submenu items (indented) */
  if (sys_submenu_open) {
    fb_draw_string(mx + 28, iy, "Terminal", 0xAAAAAA, 0x1e1e2e, 1);
    fb_draw_string(mx + 28, iy + 20, "System Monitor", 0xAAAAAA, 0x1e1e2e, 1);
    fb_draw_string(mx + 28, iy + 40, "About akaOS", 0xAAAAAA, 0x1e1e2e, 1);
    fb_draw_string(mx + 28, iy + 60, "Settings", 0xAAAAAA, 0x1e1e2e, 1);
    iy += 80;
  }

  /* "Games >" or "Games v" category */
  fb_draw_string(mx + 10, iy, games_submenu_open ? "v Games" : "> Games",
                 0x9ece6a, 0x1e1e2e, 1);
  iy += 20;

  if (games_submenu_open) {
    fb_draw_string(mx + 28, iy, "DOOM", 0xAAAAAA, 0x1e1e2e, 1);
    iy += 20;
  }

  /* Reboot */
  fb_draw_string(mx + 10, iy, "Reboot System", 0xf7768e, 0x1e1e2e, 1);
}

/* ============================================================
 * Init & Main Loop
 * ============================================================ */
void gui_init(void) {
  /* Sync display UI state to actual current renderer state */
  int curr_w = (int)fb_width() * fb_get_scale();
  int curr_h = (int)fb_height() * fb_get_scale();
  int best_idx = 2;
  int min_diff = 99999999;
  for (int i = 0; i < DISP_RES_COUNT; i++) {
    int dw = disp_res_w[i] - curr_w;
    int dh = disp_res_h[i] - curr_h;
    int diff = dw * dw + dh * dh;
    if (diff < min_diff) {
      min_diff = diff;
      best_idx = i;
    }
  }
  disp_res_idx = best_idx;

  memset(tbuf, 0, sizeof(tbuf));
  for (int r = 0; r < TERM_ROWS; r++)
    for (int c = 0; c < TERM_COLS; c++)
      tfg[r][c] = 0xa9b1d6;

  ww = TERM_COLS * CHAR_W + TERM_PAD * 2;
  wh = TERM_ROWS * CHAR_H + TITLE_H + TERM_PAD * 2;
  wx = ((int)fb_width() - ww) / 2;
  wy = ((int)fb_height() - TASKBAR_H - wh) / 2;
  if (wx < 0)
    wx = 10;
  if (wy < 0)
    wy = 10;

  ax = ((int)fb_width() - aw) / 2;
  ay = ((int)fb_height() - TASKBAR_H - ah) / 2;

  sx = ((int)fb_width() - sw) / 2;
  sy = ((int)fb_height() - TASKBAR_H - sh) / 2;

  clen = 0;
  tcx = 0;
  tcy = 0;
  start_menu_vis = 0;
  about_vis = 0;
  sysmon_vis = 0;
  settings_vis = 0;
  settings_tab = 0;

  settx = ((int)fb_width() - settw) / 2;
  setty = ((int)fb_height() - TASKBAR_H - setth) / 2;

  gui_mode_active = 1;

  sysmon_init(); /* Initialize backend tracking! */

  gui_term_set_color(0x7dcfff);
  gui_term_print("  Welcome to akaOS Terminal\n");
  gui_term_set_color(0xa9b1d6);
  gui_term_print("  Type 'help' for commands.\n\n");
  shell_print_prompt();
}

void gui_pump(void) {
  if (!gui_mode_active)
    return;

  /* Throttle to ~30 FPS to avoid locking up the system in tight polling loops
   */
  static uint64_t last_pump = 0;
  uint64_t now = timer_get_ticks();
  int hz = 60;
  if (disp_hz_idx == 1)
    hz = 75;
  if (disp_hz_idx == 2)
    hz = 120;
  if (disp_hz_idx == 3)
    hz = 144;
  int wait_ticks = 100 / hz;
  if (wait_ticks < 1)
    wait_ticks = 1;
  if (now - last_pump < (uint64_t)wait_ticks)
    return;
  last_pump = now;

  int scale = fb_get_scale();
  int mx = mouse_get_x() / scale, my = mouse_get_y() / scale;

  draw_desktop();
  draw_window(); /* Terminal (bottom) */
  if (doom_running)
    draw_doom_window();
  draw_about_window();    /* About */
  draw_sysmon_window();   /* Sysmon */
  draw_settings_window(); /* Settings (top layer) */
  draw_taskbar();
  draw_start_menu();
  draw_cursor(mx, my);
  fb_flip();
}

static int _pid_term = -1;
static int _pid_sysmon = -1;
static int _pid_settings = -1;
static int _pid_about = -1;
static int _pid_doom = -1;

static void sync_processes(void) {
  if (win_vis && _pid_term < 0)
    _pid_term = os_process_spawn("terminal", "root", 512);
  if (!win_vis && _pid_term >= 0) {
    os_process_kill(_pid_term);
    _pid_term = -1;
  }

  if (sysmon_vis && _pid_sysmon < 0)
    _pid_sysmon = os_process_spawn("sysmgr", "root", 256);
  if (!sysmon_vis && _pid_sysmon >= 0) {
    os_process_kill(_pid_sysmon);
    _pid_sysmon = -1;
  }

  if (settings_vis && _pid_settings < 0)
    _pid_settings = os_process_spawn("settings", "root", 128);
  if (!settings_vis && _pid_settings >= 0) {
    os_process_kill(_pid_settings);
    _pid_settings = -1;
  }

  if (about_vis && _pid_about < 0)
    _pid_about = os_process_spawn("about", "root", 64);
  if (!about_vis && _pid_about >= 0) {
    os_process_kill(_pid_about);
    _pid_about = -1;
  }

  if (doom_running && _pid_doom < 0)
    _pid_doom = os_process_spawn("doom", "root", 8192);
  if (!doom_running && _pid_doom >= 0) {
    os_process_kill(_pid_doom);
    _pid_doom = -1;
  }
}

void gui_run(void) {
  if (__builtin_setjmp(doom_exit_jmp_buf) != 0) {
    /* DOOM crashed or called I_Quit -> exit() */
    doom_running = 3; /* Dead/Finished */
    doom_vis = 0;
    extern void (*keyboard_event_hook)(int, int);
    keyboard_event_hook = 0; /* Unhook DOOM keys */
  }

  while (1) {
    if (doom_running == 1) {
      extern void doomgeneric_Create(int argc, char **argv);
      char *argv[] = {"doom", "-iwad", "doom1.wad", NULL};
      suppress_printf = 1;
      doomgeneric_Create(3, argv);
      suppress_printf = 0;
      doom_running = 2; /* Ready to tick */
    }

    if (doom_running == 2 && doom_vis && !doom_minimized) {
      extern void doomgeneric_Tick(void);
      suppress_printf = 1;
      doomgeneric_Tick();
      suppress_printf = 0;
    }

    sync_processes();

    int scale = fb_get_scale();
    int mx = mouse_get_x() / scale, my = mouse_get_y() / scale;
    int btn = mouse_get_buttons() & 1;

    /* Mouse clicks */
    if (btn && !prev_btn) {
      int ty = (int)fb_height() - TASKBAR_H;

      /* Start Menu button click */
      if (mx >= 4 && mx < 74 && my >= ty + 4 && my < ty + TASKBAR_H - 4) {
        start_menu_vis = !start_menu_vis;
        dragging = 0;
      }
      /* Start Menu items click */
      else if (start_menu_vis) {
        int mh = get_menu_h();
        int menu_x = 4, menu_y = ty - mh - 4;
        if (mx >= menu_x && mx < menu_x + MENU_W && my >= menu_y &&
            my < menu_y + mh) {
          int item_y = my - menu_y;
          int sys_row = 32; /* "System" label */
          int sub_start = sys_row + 20;

          if (item_y >= sys_row && item_y < sys_row + 20) {
            /* Toggle System submenu */
            sys_submenu_open = !sys_submenu_open;
          } else if (sys_submenu_open && item_y >= sub_start &&
                     item_y < sub_start + 20) {
            /* Terminal */
            win_vis = 1;
            start_menu_vis = 0;
            sys_submenu_open = 0;
          } else if (sys_submenu_open && item_y >= sub_start + 20 &&
                     item_y < sub_start + 40) {
            /* System Monitor */
            sysmon_vis = 1;
            start_menu_vis = 0;
            sys_submenu_open = 0;
          } else if (sys_submenu_open && item_y >= sub_start + 40 &&
                     item_y < sub_start + 60) {
            /* About akaOS */
            about_vis = 1;
            start_menu_vis = 0;
            sys_submenu_open = 0;
          } else if (sys_submenu_open && item_y >= sub_start + 60 &&
                     item_y < sub_start + 80) {
            /* Settings */
            settings_vis = 1;
            start_menu_vis = 0;
            sys_submenu_open = 0;
            /* Sync display UI state to actual current renderer state */
            int curr_w = (int)fb_width() * fb_get_scale();
            int curr_h = (int)fb_height() * fb_get_scale();
            int best_idx = 2;
            int min_diff = 99999999;
            for (int i = 0; i < DISP_RES_COUNT; i++) {
              int dw = disp_res_w[i] - curr_w;
              int dh = disp_res_h[i] - curr_h;
              int diff = dw * dw + dh * dh;
              if (diff < min_diff) {
                min_diff = diff;
                best_idx = i;
              }
            }
            disp_res_idx = best_idx;
          } else {
            int games_row = sub_start + (sys_submenu_open ? 80 : 0);
            int games_sub_start = games_row + 20;

            if (item_y >= games_row && item_y < games_row + 20) {
              /* Toggle Games submenu */
              games_submenu_open = !games_submenu_open;
            } else if (games_submenu_open && item_y >= games_sub_start &&
                       item_y < games_sub_start + 20) {
              /* DOOM */
              start_menu_vis = 0;
              sys_submenu_open = 0;
              games_submenu_open = 0;
              if (doom_running == 0) {
                doom_running = 1;
                doom_vis = 1;
                doom_minimized = 0;
                doom_maximized = 0;
                doom_x = 100;
                doom_y = 100;
              } else if (doom_running == 2) {
                doom_vis = 1;
                doom_minimized = 0;
              } else if (doom_running == 3) {
                gui_term_set_color(0xf7768e);
                gui_term_print("DOOM has exited and cannot be restarted "
                               "gracefully.\nReboot OS to play again.\n");
                win_vis = 1;
              }
            } else {
              /* Reboot row: last item after all submenus */
              int reboot_y = games_sub_start + (games_submenu_open ? 20 : 0);
              if (item_y >= reboot_y && item_y < reboot_y + 20) {
                uint8_t g = 0x02;
                while (g & 0x02)
                  g = inb(0x64);
                outb(0x64, 0xFE);
                asm volatile("cli; hlt");
              }
            }
          }
        } else {
          /* Clicked outside menu */
          start_menu_vis = 0;
          sys_submenu_open = 0;
        }
      }
      /* Window dragging & closing (only if menu is closed or clicked outside
         it) */
      else {
        if (start_menu_vis) {
          start_menu_vis = 0;
          sys_submenu_open = 0;
        } /* Auto-close on outside click */

        int clicked_win = 0;

        /* Settings Window (top most) */
        if (settings_vis && !clicked_win) {
          if (mx >= settx && mx < settx + settw && my >= setty &&
              my < setty + TITLE_H) {
            if (mx >= settx + settw - 20 && mx < settx + settw - 4 &&
                my >= setty + 3 && my < setty + 19) {
              settings_vis = 0; /* Close */
            } else {
              dragging = 4;
              dox = mx - settx;
              doy = my - setty; /* Drag */
            }
            clicked_win = 1;
          } else if (mx >= settx && mx < settx + SETTINGS_SIDEBAR_W &&
                     my >= setty + TITLE_H && my < setty + setth) {
            /* Tab clicks */
            int rel_y = my - (setty + TITLE_H) - 8;
            int tab_idx = rel_y / 28;
            if (tab_idx >= 0 && tab_idx < SETTINGS_TAB_COUNT) {
              settings_tab = tab_idx;
            }
            clicked_win = 1;
          } else if (mx >= settx + SETTINGS_SIDEBAR_W && mx < settx + settw &&
                     my >= setty + TITLE_H && my < setty + setth &&
                     settings_tab == 0) {
            /* Display tab content clicks — detect arrow buttons */
            int content_x = settx + SETTINGS_SIDEBAR_W + 1;
            int content_y = setty + TITLE_H;
            int ctrl_abs_x = content_x + DISP_CTRL_X;
            int left_x1 = ctrl_abs_x;
            int left_x2 = ctrl_abs_x + DISP_ARROW_W;
            int right_x1 = ctrl_abs_x + DISP_ARROW_W + 160;
            int right_x2 = right_x1 + DISP_ARROW_W;
            /* Check each row */
            int rows_y[] = {DISP_ROW_RES, DISP_ROW_SCALE, DISP_ROW_HZ,
                            DISP_ROW_DEPTH};
            int *idxs[] = {&disp_res_idx, &disp_scale_idx, &disp_hz_idx,
                           &disp_depth_idx};
            int maxs[] = {DISP_RES_COUNT, DISP_SCALE_COUNT, DISP_HZ_COUNT,
                          DISP_DEPTH_COUNT};
            for (int i = 0; i < 4; i++) {
              int ry = content_y + rows_y[i] - 2;
              if (my >= ry && my < ry + DISP_ROW_H) {
                if (mx >= left_x1 && mx < left_x2 && *idxs[i] > 0) {
                  (*idxs[i])--;
                } else if (mx >= right_x1 && mx < right_x2 &&
                           *idxs[i] < maxs[i] - 1) {
                  (*idxs[i])++;
                }
                break;
              }
            }
            /* Apply button click */
            int cw_inner = settw - SETTINGS_SIDEBAR_W - 1;
            int ch_inner = setth - TITLE_H;
            int abx = content_x + cw_inner - 120;
            int aby = content_y + ch_inner - 30;
            if (mx >= abx && mx < abx + 100 && my >= aby && my < aby + 22) {
              /* Apply render params first (scale and depth) */
              int scale_val = disp_scale_idx + 1; /* 0=1x, 1=2x, 2=3x */
              int depth = (disp_depth_idx == 0)   ? 16
                          : (disp_depth_idx == 1) ? 24
                                                  : 32;
              fb_set_render_params(scale_val, depth);
              /* Apply resolution immediately */
              fb_set_resolution(disp_res_w[disp_res_idx],
                                disp_res_h[disp_res_idx]);
              /* Recenter all windows */
              int nw = (int)fb_width(), nh = (int)fb_height();
              mouse_set_bounds(nw * fb_get_scale(), nh * fb_get_scale());
              wx = (nw - ww) / 2;
              wy = (nh - TASKBAR_H - wh) / 2;
              if (wx < 0)
                wx = 10;
              if (wy < 0)
                wy = 10;
              ax = (nw - aw) / 2;
              ay = (nh - TASKBAR_H - ah) / 2;
              sx = (nw - sw) / 2;
              sy = (nh - TASKBAR_H - sh) / 2;
              settx = (nw - settw) / 2;
              setty = (nh - TASKBAR_H - setth) / 2;
            }
            clicked_win = 1;
            active_window_focus = 4;
          } else if (mx >= settx && mx < settx + settw && my >= setty &&
                     my < setty + setth) {
            clicked_win = 1;
            active_window_focus = 4;
          }
        }

        /* System Monitor Window */
        if (sysmon_vis && !clicked_win) {
          if (mx >= sx && mx < sx + sw && my >= sy && my < sy + TITLE_H) {
            if (mx >= sx + sw - 20 && mx < sx + sw - 4 && my >= sy + 4 &&
                my < sy + 20) {
              sysmon_vis = 0; /* Close */
            } else {
              dragging = 3;
              dox = mx - sx;
              doy = my - sy; /* Drag */
            }
            clicked_win = 1;
            active_window_focus = 3;
          } else if (mx >= sx && mx < sx + sw && my >= sy && my < sy + sh) {
            clicked_win = 1;
            active_window_focus = 3;
          }
        }

        /* About Window (z-index middle) */
        if (about_vis && !clicked_win) {
          if (mx >= ax && mx < ax + aw && my >= ay && my < ay + TITLE_H) {
            if (mx >= ax + 8 && mx < ax + 22 && my >= ay + 4 && my < ay + 18) {
              about_vis = 0; /* Close */
            } else {
              dragging = 2;
              dox = mx - ax;
              doy = my - ay; /* Drag */
            }
            clicked_win = 1;
            active_window_focus = 2;
          } else if (mx >= ax && mx < ax + aw && my >= ay && my < ay + ah) {
            clicked_win = 1;
            active_window_focus = 2;
          }
        }

        /* DOOM Window */
        if (doom_running && doom_vis && !doom_minimized && !clicked_win) {
          int dw = doom_maximized ? (int)fb_width() : doom_w;
          int dh = doom_maximized ? ((int)fb_height() - TASKBAR_H - TITLE_H)
                                  : doom_h;
          int bx = doom_maximized ? 0 : doom_x;
          int by = doom_maximized ? 0 : doom_y;

          if (mx >= bx && mx < bx + dw && my >= by && my < by + TITLE_H) {
            if (mx >= bx + dw - 20 && mx < bx + dw - 4 && my >= by + 3 &&
                my < by + 19) {
              /* Close (Suspend DOOM) */
              doom_vis = 0;
              gui_term_set_color(0xe0af68);
              gui_term_print("DOOM suspended. Bare-metal processes cannot be "
                             "killed cleanly without a reboot.\n");
              win_vis = 1;
            } else if (mx >= bx + dw - 40 && mx < bx + dw - 24 &&
                       my >= by + 3 && my < by + 19) {
              doom_maximized = !doom_maximized;
            } else if (mx >= bx + dw - 60 && mx < bx + dw - 44 &&
                       my >= by + 3 && my < by + 19) {
              doom_minimized = 1;
            } else if (!doom_maximized) {
              dragging = 5;
              dox = mx - doom_x;
              doy = my - doom_y; /* Drag */
            }
            clicked_win = 1;
            active_window_focus = 5;
          } else if (mx >= bx && mx < bx + dw && my >= by &&
                     my < by + dh + TITLE_H) {
            clicked_win = 1;
            active_window_focus = 5;
            /* Gain keyboard focus implicitly (this is standard) */
          }
        }

        /* Terminal Window (bottom) */
        if (win_vis && !clicked_win) {
          if (mx >= wx && mx < wx + ww && my >= wy && my < wy + TITLE_H) {
            if (mx >= wx + ww - 20 && mx < wx + ww - 4 && my >= wy + 3 &&
                my < wy + 19) {
              win_vis = 0; /* Close */
            } else {
              dragging = 1;
              dox = mx - wx;
              doy = my - wy; /* Drag */
            }
            clicked_win = 1;
            active_window_focus = 1;
          } else if (mx >= wx && mx < wx + ww && my >= wy && my < wy + wh) {
            clicked_win = 1;
            active_window_focus = 1;
          }
        }

        /* Desktop Click */
        if (!clicked_win && my < ty) {
          active_window_focus = 0;
        }

        /* Taskbar Terminal button */
        if (mx >= 80 && mx < 160 && my >= ty + 4 && my < ty + TASKBAR_H - 4) {
          win_vis = !win_vis;
        }

        /* Taskbar DOOM button */
        if (doom_running && mx >= 170 && mx < 250 && my >= ty + 4 &&
            my < ty + TASKBAR_H - 4) {
          if (doom_minimized)
            doom_minimized = 0;
          else
            doom_vis = !doom_vis;
        }
      }
    }
    if (!btn)
      dragging = 0;

    if (dragging == 1 && win_vis) {
      wx = mx - dox;
      wy = my - doy;
      if (wx < 0)
        wx = 0;
      if (wy < 0)
        wy = 0;
    } else if (dragging == 2 && about_vis) {
      ax = mx - dox;
      ay = my - doy;
      if (ax < 0)
        ax = 0;
      if (ay < 0)
        ay = 0;
    } else if (dragging == 3 && sysmon_vis) {
      sx = mx - dox;
      sy = my - doy;
      if (sx < 0)
        sx = 0;
      if (sy < 0)
        sy = 0;
    } else if (dragging == 4 && settings_vis) {
      settx = mx - dox;
      setty = my - doy;
      if (settx < 0)
        settx = 0;
      if (setty < 0)
        setty = 0;
    } else if (dragging == 5 && doom_running && doom_vis && !doom_maximized) {
      doom_x = mx - dox;
      doom_y = my - doy;
      if (doom_x < 0)
        doom_x = 0;
      if (doom_y < 0)
        doom_y = 0;
    }
    prev_btn = btn;

    /* Keyboard */
    while (keyboard_has_char()) {
      char c = keyboard_getchar();
      if (win_vis && active_window_focus == 1)
        handle_key(c);
    }

    /* Render */
    gui_pump();

    extern void sysmon_record_idle_start(void);
    extern void sysmon_record_idle_end(void);
    sysmon_record_idle_start();
    asm volatile("hlt");
    sysmon_record_idle_end();
  }
}
