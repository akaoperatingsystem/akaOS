/* ============================================================
 * akaOS — GUI Desktop (Simplified, Robust)
 * ============================================================ */
#include "gui.h"
#include "fb.h"
#include "fs.h"
#include "io.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
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
#define BTN_SZ 16
#define BTN_PAD 3
#define RESIZE_GRIP 12

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
static int doom_prev_x = 100, doom_prev_y = 100, doom_prev_w = 640,
           doom_prev_h = 400;
extern uint32_t *DG_ScreenBuffer;

/* Window state */
int active_window_focus =
    1; /* 0=Desktop, 1=Term, 2=About, 3=Sysmon, 4=Settings, 5=DOOM, 6=Bench, 7=Files, 8=Calc, 9=Notepad, 10=Browser */
int wx, wy, ww, wh;
int win_vis = 0;
static int win_minimized = 0;
static int win_maximized = 0;
static int win_prev_x = 0, win_prev_y = 0, win_prev_w = 0, win_prev_h = 0;
int about_vis = 0;
static int about_minimized = 0;
static int about_maximized = 0;
static int about_prev_x = 0, about_prev_y = 0, about_prev_w = 450,
           about_prev_h = 200;
int sysmon_vis = 0;
static int sysmon_minimized = 0;
static int sysmon_maximized = 0;
static int sysmon_prev_x = 0, sysmon_prev_y = 0, sysmon_prev_w = 480,
           sysmon_prev_h = 340;
static int sysmon_core_offset = 0; /* for paging core list when many cores */
static int sysmon_pager_vis = 0;
static int sysmon_pager_step = 0;
static int sysmon_last_core_count = 0;
static int sysmon_pager_prev_x = 0, sysmon_pager_prev_y = 0;
static int sysmon_pager_next_x = 0, sysmon_pager_next_y = 0;
static int sysmon_pager_btn_w = 14, sysmon_pager_btn_h = 14;
static int sysmon_tab = 0; /* 0=Performance, 1=Processes */
static int sysmon_tab_perf_x = 0, sysmon_tab_perf_y = 0, sysmon_tab_perf_w = 0,
           sysmon_tab_perf_h = 0;
static int sysmon_tab_proc_x = 0, sysmon_tab_proc_y = 0, sysmon_tab_proc_w = 0,
           sysmon_tab_proc_h = 0;
static int sysmon_selected_pid = -1;
static int sysmon_visible_pids[MAX_PROCS];
static int sysmon_visible_pid_count = 0;
static int sysmon_proc_rows_y = 0;
static int sysmon_proc_rows_h = 14;
static int sysmon_proc_rows_x1 = 0;
static int sysmon_proc_rows_x2 = 0;
static int sysmon_end_btn_x = 0, sysmon_end_btn_y = 0, sysmon_end_btn_w = 86,
           sysmon_end_btn_h = 18;
static int sysmon_end_btn_enabled = 0;
int bench_vis = 0;
static int bench_minimized = 0;
static int bench_maximized = 0;
static int bench_prev_x = 0, bench_prev_y = 0, bench_prev_w = 520,
           bench_prev_h = 360;
static int bx, by, bw = 520, bh = 360;
int files_vis = 0;
static int files_minimized = 0;
static int files_maximized = 0;
static int files_prev_x = 0, files_prev_y = 0, files_prev_w = 560,
           files_prev_h = 380;
static int fx, fy, fw = 560, fh = 380;
int calc_vis = 0;
static int calc_minimized = 0;
static int calc_maximized = 0;
static int calc_prev_x = 0, calc_prev_y = 0, calc_prev_w = 280, calc_prev_h = 360;
static int calcx, calcy, calcw = 280, calch = 360;
int note_vis = 0;
static int note_minimized = 0;
static int note_maximized = 0;
static int note_prev_x = 0, note_prev_y = 0, note_prev_w = 520, note_prev_h = 380;
static int notex, notey, notew = 520, noteh = 380;
int browser_vis = 0;
static int browser_minimized = 0;
static int browser_maximized = 0;
static int browser_prev_x = 0, browser_prev_y = 0, browser_prev_w = 600,
           browser_prev_h = 420;
static int brx, bry, brw = 600, brh = 420;
int settings_vis = 0;
static int settings_minimized = 0;
static int settings_maximized = 0;
static int settings_prev_x = 0, settings_prev_y = 0, settings_prev_w = 460,
           settings_prev_h = 300;

static int dragging = 0, dox = 0, doy = 0;
static int prev_btn = 0;
static int resize_base_w = 0, resize_base_h = 0;
static int resize_start_x = 0, resize_start_y = 0;

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

void gui_term_clear(void) {
  /* Clear the GUI terminal buffer (used by the shell's `clear` command). */
  for (int r = 0; r < TERM_ROWS; r++) {
    for (int c = 0; c < TERM_COLS; c++) {
      tbuf[r][c] = ' ';
      tfg[r][c] = 0xa9b1d6;
    }
  }
  tcx = 0;
  tcy = 0;
}

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

  /* Clock (drawn last, but we need its x for layout) */
  char up[32];
  timer_format_uptime(up, 32);
  int cl = (int)strlen(up);
  int clock_x = w - cl * CHAR_W - 8;

  /* akaOS button */
  fb_fill_rect(4, ty + 4, 70, TASKBAR_H - 8, 0x3d3d5c);
  fb_draw_string(10, ty + 11, "akaOS", 0x7dcfff, 0x3d3d5c, 1);

  /* Terminal button */
  uint32_t tbc = (win_vis && !win_minimized) ? 0x4a4a6a : 0x2d2d44;
  fb_fill_rect(80, ty + 4, 80, TASKBAR_H - 8, tbc);
  fb_draw_string(88, ty + 11, "Terminal", 0xFFFFFF, tbc, 1);

  /* DOOM button */
  int x = 170;
  if (doom_running) {
    uint32_t dbc = (doom_vis && !doom_minimized) ? 0x4a4a6a : 0x2d2d44;
    fb_fill_rect(x, ty + 4, 80, TASKBAR_H - 8, dbc);
    fb_draw_string(x + 8, ty + 11, "DOOM", 0xFFFFFF, dbc, 1);
    x += 90;
  }

  /* Other app buttons (shown when app exists, even if minimized) */
  const int bw_btn = 90;
  const int gap = 8;

  if (about_vis && x + bw_btn < clock_x - 6) {
    uint32_t bc = (about_vis && !about_minimized && active_window_focus == 2)
                      ? 0x4a4a6a
                      : 0x2d2d44;
    fb_fill_rect(x, ty + 4, bw_btn, TASKBAR_H - 8, bc);
    fb_draw_string(x + 10, ty + 11, "About", 0xFFFFFF, bc, 1);
    x += bw_btn + gap;
  }
  if (sysmon_vis && x + bw_btn < clock_x - 6) {
    uint32_t bc = (sysmon_vis && !sysmon_minimized && active_window_focus == 3)
                      ? 0x4a4a6a
                      : 0x2d2d44;
    fb_fill_rect(x, ty + 4, bw_btn, TASKBAR_H - 8, bc);
    fb_draw_string(x + 10, ty + 11, "Monitor", 0xFFFFFF, bc, 1);
    x += bw_btn + gap;
  }
  if (settings_vis && x + bw_btn < clock_x - 6) {
    uint32_t bc = (settings_vis && !settings_minimized && active_window_focus == 4)
                      ? 0x4a4a6a
                      : 0x2d2d44;
    fb_fill_rect(x, ty + 4, bw_btn, TASKBAR_H - 8, bc);
    fb_draw_string(x + 10, ty + 11, "Settings", 0xFFFFFF, bc, 1);
    x += bw_btn + gap;
  }
  if (bench_vis && x + bw_btn < clock_x - 6) {
    uint32_t bc =
        (bench_vis && !bench_minimized && active_window_focus == 6) ? 0x4a4a6a
                                                                    : 0x2d2d44;
    fb_fill_rect(x, ty + 4, bw_btn, TASKBAR_H - 8, bc);
    fb_draw_string(x + 10, ty + 11, "Benchmark", 0xFFFFFF, bc, 1);
    x += bw_btn + gap;
  }
  if (files_vis && x + bw_btn < clock_x - 6) {
    uint32_t bc =
        (files_vis && !files_minimized && active_window_focus == 7) ? 0x4a4a6a
                                                                    : 0x2d2d44;
    fb_fill_rect(x, ty + 4, bw_btn, TASKBAR_H - 8, bc);
    fb_draw_string(x + 10, ty + 11, "Files", 0xFFFFFF, bc, 1);
    x += bw_btn + gap;
  }
  if (calc_vis && x + bw_btn < clock_x - 6) {
    uint32_t bc =
        (calc_vis && !calc_minimized && active_window_focus == 8) ? 0x4a4a6a
                                                                  : 0x2d2d44;
    fb_fill_rect(x, ty + 4, bw_btn, TASKBAR_H - 8, bc);
    fb_draw_string(x + 10, ty + 11, "Calc", 0xFFFFFF, bc, 1);
    x += bw_btn + gap;
  }
  if (note_vis && x + bw_btn < clock_x - 6) {
    uint32_t bc =
        (note_vis && !note_minimized && active_window_focus == 9) ? 0x4a4a6a
                                                                  : 0x2d2d44;
    fb_fill_rect(x, ty + 4, bw_btn, TASKBAR_H - 8, bc);
    fb_draw_string(x + 10, ty + 11, "Notepad", 0xFFFFFF, bc, 1);
    x += bw_btn + gap;
  }
  if (browser_vis && x + bw_btn < clock_x - 6) {
    uint32_t bc = (browser_vis && !browser_minimized && active_window_focus == 10)
                      ? 0x4a4a6a
                      : 0x2d2d44;
    fb_fill_rect(x, ty + 4, bw_btn, TASKBAR_H - 8, bc);
    fb_draw_string(x + 10, ty + 11, "Browser", 0xFFFFFF, bc, 1);
    x += bw_btn + gap;
  }

  /* Clock */
  fb_draw_string(clock_x, ty + 11, up, 0xAAAAAA, 0x1e1e2e, 1);
}

static void draw_window(void) {
  if (!win_vis || win_minimized)
    return;

  /* Shadow */
  fb_fill_rect(wx + 3, wy + 3, ww, wh, 0x080808);

  /* Title bar */
  fb_fill_rect(wx, wy, ww, TITLE_H, 0x4a4a6a);
  fb_draw_string(wx + 8, wy + 7, "Terminal", 0xFFFFFF, 0x4a4a6a, 1);

  /* Window buttons: [ - ] [ + ] [ x ] */
  int bclose = wx + ww - (BTN_PAD + BTN_SZ);
  int bmax = bclose - (BTN_SZ + 4);
  int bmin = bmax - (BTN_SZ + 4);
  fb_fill_rect(bclose, wy + BTN_PAD, BTN_SZ, BTN_SZ, 0xf7768e);
  fb_draw_char(bclose + 2, wy + 7, 'x', 0xFFFFFF, 0xf7768e, 1);
  fb_fill_rect(bmax, wy + BTN_PAD, BTN_SZ, BTN_SZ, 0x9ece6a);
  fb_draw_char(bmax + 2, wy + 7, '+', 0xFFFFFF, 0x9ece6a, 1);
  fb_fill_rect(bmin, wy + BTN_PAD, BTN_SZ, BTN_SZ, 0xe0af68);
  fb_draw_char(bmin + 2, wy + 7, '-', 0xFFFFFF, 0xe0af68, 1);

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

  /* Resize grip */
  if (!win_maximized) {
    fb_fill_rect(wx + ww - RESIZE_GRIP, wy + wh - RESIZE_GRIP, RESIZE_GRIP,
                 RESIZE_GRIP, 0x24283b);
    fb_draw_char(wx + ww - 10, wy + wh - 10, '\\', 0x565f89, 0x24283b, 1);
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

  /* Resize grip */
  if (!doom_maximized) {
    fb_fill_rect(bx + dw - RESIZE_GRIP, by + dh + TITLE_H - RESIZE_GRIP,
                 RESIZE_GRIP, RESIZE_GRIP, 0x24283b);
    fb_draw_char(bx + dw - 10, by + dh + TITLE_H - 10, '\\', 0x565f89,
                 0x24283b, 1);
  }

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
    /* Slow path: scaling (maximized/resized) — fixed-point stepping */
    if (dx < 0 || dy < 0 || dx + dw > bbw || dy + dh > bbh) {
      /* Safety: keep old behavior when clipping would be required */
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
    } else {
      const int32_t x_step = (int32_t)((src_w << 16) / dw);
      const int32_t y_step = (int32_t)((src_h << 16) / dh);
      int32_t y_acc = 0;
      for (int y = 0; y < dh; y++) {
        int sy = (y_acc >> 16);
        if (sy >= src_h)
          sy = src_h - 1;
        uint32_t *dst_row = &bb[(dy + y) * bbw + dx];
        uint32_t *src_row = &DG_ScreenBuffer[sy * src_w];
        int32_t x_acc = 0;
        for (int x = 0; x < dw; x++) {
          int sx = (x_acc >> 16);
          if (sx >= src_w)
            sx = src_w - 1;
          dst_row[x] = src_row[sx];
          x_acc += x_step;
        }
        y_acc += y_step;
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

/* Forward decl: used by About/Settings before the helper definitions below. */
static void draw_string_ellipsis(int x, int y, const char *s, uint32_t fg,
                                 uint32_t bg, int scale, int max_px);

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

  /* Trim leading/trailing spaces so long model strings don't waste width. */
  while (*buf == ' ')
    memmove(buf, buf + 1, strlen(buf));
  int n = (int)strlen(buf);
  while (n > 0 && buf[n - 1] == ' ') {
    buf[n - 1] = '\0';
    n--;
  }
}

static void draw_about_window(void) {
  if (!about_vis || about_minimized)
    return;

  /* Shadow */
  fb_fill_rect(ax + 5, ay + 5, aw, ah, 0x050505);

  /* Main body (macOS-like rounded/sleek feel, though we use rects) */
  fb_fill_rect(ax, ay, aw, ah, 0x1e1e2e);
  fb_draw_rect(ax, ay, aw, ah, 0x565f89);

  /* Title bar */
  fb_fill_rect(ax, ay, aw, TITLE_H, 0x2a2a4a);

  /* Window buttons: [ - ] [ x ] */
  int bclose = ax + aw - (BTN_PAD + BTN_SZ);
  int bmin = bclose - (BTN_SZ + 4);
  fb_fill_rect(bclose, ay + BTN_PAD, BTN_SZ, BTN_SZ, 0xf7768e);
  fb_draw_char(bclose + 2, ay + 7, 'x', 0xFFFFFF, 0xf7768e, 1);
  fb_fill_rect(bmin, ay + BTN_PAD, BTN_SZ, BTN_SZ, 0xe0af68);
  fb_draw_char(bmin + 2, ay + 7, '-', 0xFFFFFF, 0xe0af68, 1);

  /* Top Title */
  fb_draw_string(ax + aw / 2 - (5 * 8), ay + 40, "akaOS", 0x7dcfff, 0x1e1e2e,
                 2); /* Scaled up */
  fb_draw_string(ax + 20, ay + 80, "Version:     1.1 (x86_64)", 0xAAAAAA,
                 0x1e1e2e, 1);

  /* Real Specs */
  char cpu_str[64];
  memset(cpu_str, 0, sizeof(cpu_str));
  get_cpu_string(cpu_str);
  fb_draw_string(ax + 20, ay + 100, "Processor:   ", 0xAAAAAA, 0x1e1e2e, 1);
  draw_string_ellipsis(ax + 124, ay + 100, cpu_str, 0xFFFFFF, 0x1e1e2e, 1,
                       (ax + aw - 20) - (ax + 124));

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
 * Benchmark Window
 * ============================================================ */
static inline uint64_t bench_rdtsc(void) {
  uint32_t lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

static int bench_mode = 2; /* 0=CPU, 1=RAM, 2=Both */
static int bench_running = 0;
static int bench_phase = 0; /* 0=idle, 1=cpu, 2=ram, 3=done */
static int bench_progress = 0;
static uint64_t bench_cpu_cycles = 0;
static uint64_t bench_ram_cycles = 0;
static uint32_t bench_ram_kb_moved = 0;
static uint32_t bench_cpu_iters = 0;
static volatile uint32_t bench_sink = 0;
static uint32_t bench_cpu_done = 0, bench_ram_done = 0;
static uint32_t bench_cpu_total = 9000000;
static uint32_t bench_ram_total = 256;
static uint64_t bench_phase_start = 0;
static uint8_t bench_src[256 * 1024];
static uint8_t bench_dst[256 * 1024];

static void bench_reset(void) {
  bench_running = 0;
  bench_phase = 0;
  bench_progress = 0;
  bench_cpu_cycles = 0;
  bench_ram_cycles = 0;
  bench_ram_kb_moved = 0;
  bench_cpu_iters = 0;
  bench_sink = 0;
  bench_cpu_done = 0;
  bench_ram_done = 0;
  bench_phase_start = 0;
}

static void bench_start(void) {
  bench_reset();
  bench_running = 1;
  bench_phase = (bench_mode == 1) ? 2 : 1;
  bench_phase_start = bench_rdtsc();
  /* Touch buffers so RAM benchmark isn't dominated by first-touch. */
  for (int i = 0; i < (int)sizeof(bench_src); i += 64) {
    bench_src[i] = (uint8_t)(i & 0xFF);
    bench_dst[i] = 0;
  }
}

static void bench_step(void) {
  if (!bench_running)
    return;

  /* Time-slice work to keep UI responsive. */
  uint64_t t0 = bench_rdtsc();
  const uint64_t budget = 6000000ULL; /* ~small slice; depends on CPU */

  while ((bench_rdtsc() - t0) < budget) {
    if (bench_phase == 1) {
      /* CPU benchmark: integer mix. */
      uint32_t local = bench_sink;
      uint32_t remaining = bench_cpu_total - bench_cpu_done;
      uint32_t chunk = (remaining > 50000) ? 50000 : remaining;
      for (uint32_t i = 0; i < chunk; i++) {
        local ^= (local << 5) + (local >> 2) + 0x9e3779b9;
        local += (local * 3) ^ (local >> 11);
      }
      bench_sink = local;
      bench_cpu_done += chunk;
      bench_cpu_iters += chunk;
      if (bench_cpu_done >= bench_cpu_total) {
        bench_cpu_cycles = bench_rdtsc() - bench_phase_start;
        if (bench_mode == 2) {
          bench_phase = 2;
          bench_phase_start = bench_rdtsc();
        } else {
          bench_phase = 3;
          bench_running = 0;
        }
      }
    } else if (bench_phase == 2) {
      /* RAM benchmark: memcpy over a fixed buffer. */
      uint32_t remaining = bench_ram_total - bench_ram_done;
      uint32_t chunk = (remaining > 2) ? 2 : remaining;
      for (uint32_t i = 0; i < chunk; i++) {
        memcpy(bench_dst, bench_src, sizeof(bench_src));
        memcpy(bench_src, bench_dst, sizeof(bench_src));
      }
      bench_ram_done += chunk;
      bench_ram_kb_moved += (uint32_t)((sizeof(bench_src) * 2ULL * chunk) / 1024ULL);
      if (bench_ram_done >= bench_ram_total) {
        bench_ram_cycles = bench_rdtsc() - bench_phase_start;
        bench_phase = 3;
        bench_running = 0;
      }
    } else {
      bench_running = 0;
      break;
    }
  }

  /* Progress */
  if (bench_mode == 0) {
    bench_progress = (int)((bench_cpu_done * 100ULL) / bench_cpu_total);
  } else if (bench_mode == 1) {
    bench_progress = (int)((bench_ram_done * 100ULL) / bench_ram_total);
  } else {
    uint64_t a = (bench_cpu_done * 50ULL) / bench_cpu_total;
    uint64_t b = (bench_ram_done * 50ULL) / bench_ram_total;
    bench_progress = (int)(a + b);
  }
  if (bench_progress > 100)
    bench_progress = 100;
}

static void draw_bench_window(void) {
  if (!bench_vis || bench_minimized)
    return;

  /* Shadow */
  fb_fill_rect(bx + 4, by + 4, bw, bh, 0x050505);

  /* Background */
  fb_fill_rect(bx, by, bw, bh, 0x1a1b26);
  fb_draw_rect(bx, by, bw, bh, 0x3d3d5c);

  /* Title bar */
  fb_fill_rect(bx, by, bw, TITLE_H, 0x3d3d5c);
  fb_draw_string(bx + 8, by + 7, "Benchmark", 0xFFFFFF, 0x3d3d5c, 1);

  /* Window buttons: [ - ] [ + ] [ x ] */
  int bclose = bx + bw - (BTN_PAD + BTN_SZ);
  int bmax = bclose - (BTN_SZ + 4);
  int bmin = bmax - (BTN_SZ + 4);
  fb_fill_rect(bclose, by + BTN_PAD, BTN_SZ, BTN_SZ, 0xf7768e);
  fb_draw_char(bclose + 2, by + 7, 'x', 0xFFFFFF, 0xf7768e, 1);
  fb_fill_rect(bmax, by + BTN_PAD, BTN_SZ, BTN_SZ, 0x9ece6a);
  fb_draw_char(bmax + 2, by + 7, '+', 0xFFFFFF, 0x9ece6a, 1);
  fb_fill_rect(bmin, by + BTN_PAD, BTN_SZ, BTN_SZ, 0xe0af68);
  fb_draw_char(bmin + 2, by + 7, '-', 0xFFFFFF, 0xe0af68, 1);

  /* Content */
  int cx = bx + 14;
  int cy = by + TITLE_H + 14;

  fb_draw_string(cx, cy, "Run:", 0xAAAAAA, 0x1a1b26, 1);
  int opt_y = cy + 18;
  int opt_w = 90;
  const char *opts[] = {"CPU", "RAM", "Both"};
  for (int i = 0; i < 3; i++) {
    uint32_t bg = (bench_mode == i) ? 0x24283b : 0x16161f;
    uint32_t fg = (bench_mode == i) ? 0x7dcfff : 0xAAAAAA;
    fb_fill_rect(cx + i * (opt_w + 8), opt_y, opt_w, 18, bg);
    fb_draw_rect(cx + i * (opt_w + 8), opt_y, opt_w, 18, 0x565f89);
    fb_draw_string(cx + i * (opt_w + 8) + 12, opt_y + 5, opts[i], fg, bg, 1);
  }

  int btn_y = opt_y + 34;
  int btn_w = 110;
  uint32_t s_bg = bench_running ? 0x24283b : 0x3d3d5c;
  uint32_t s_fg = bench_running ? 0x565f89 : 0x9ece6a;
  fb_fill_rect(cx, btn_y, btn_w, 20, s_bg);
  fb_draw_rect(cx, btn_y, btn_w, 20, 0x565f89);
  fb_draw_string(cx + 34, btn_y + 6, "Start", s_fg, s_bg, 1);

  uint32_t st_bg = bench_running ? 0x3d3d5c : 0x24283b;
  uint32_t st_fg = bench_running ? 0xf7768e : 0x565f89;
  fb_fill_rect(cx + btn_w + 10, btn_y, btn_w, 20, st_bg);
  fb_draw_rect(cx + btn_w + 10, btn_y, btn_w, 20, 0x565f89);
  fb_draw_string(cx + btn_w + 10 + 36, btn_y + 6, "Stop", st_fg, st_bg, 1);

  /* Progress */
  int py = btn_y + 36;
  fb_draw_string(cx, py, "Progress", 0xAAAAAA, 0x1a1b26, 1);
  fb_fill_rect(cx, py + 14, bw - 28, 14, 0x24283b);
  int fill = ((bw - 28) * bench_progress) / 100;
  if (fill > 0)
    fb_fill_rect(cx, py + 14, fill, 14, 0x7aa2f7);
  char pstr[16];
  int_to_str(bench_progress, pstr);
  strcat(pstr, "%");
  fb_draw_string(cx + bw - 70, py, pstr, 0xFFFFFF, 0x1a1b26, 1);

  /* Results */
  int ry = py + 44;
  fb_draw_string(cx, ry, "Results", 0xAAAAAA, 0x1a1b26, 1);
  ry += 18;
  if (bench_cpu_cycles) {
    char s1[48];
    memset(s1, 0, sizeof(s1));
    strcat(s1, "CPU cycles: ");
    char t[20];
    int_to_str((int)(bench_cpu_cycles & 0x7FFFFFFF), t);
    strcat(s1, t);
    fb_draw_string(cx, ry, s1, 0xFFFFFF, 0x1a1b26, 1);
    ry += 14;
  }
  if (bench_ram_cycles) {
    char s2[64];
    memset(s2, 0, sizeof(s2));
    strcat(s2, "RAM moved: ");
    char t1[16];
    int_to_str((int)(bench_ram_kb_moved / 1024), t1);
    strcat(s2, t1);
    strcat(s2, " MB  cycles: ");
    char t2[20];
    int_to_str((int)(bench_ram_cycles & 0x7FFFFFFF), t2);
    strcat(s2, t2);
    fb_draw_string(cx, ry, s2, 0xFFFFFF, 0x1a1b26, 1);
    ry += 14;
  }

  if (!bench_running && bench_progress == 0) {
    fb_draw_string(cx, ry + 10, "Tip: Run Both for a quick score check.",
                   0x565f89, 0x1a1b26, 1);
  }

  /* Resize grip */
  if (!bench_maximized) {
    fb_fill_rect(bx + bw - RESIZE_GRIP, by + bh - RESIZE_GRIP, RESIZE_GRIP,
                 RESIZE_GRIP, 0x24283b);
    fb_draw_char(bx + bw - 10, by + bh - 10, '\\', 0x565f89, 0x24283b, 1);
  }
}

/* ============================================================
 * File Manager Window
 * ============================================================ */
static fs_node_t *fm_dir = 0;
static int fm_selected = -1;
static uint64_t fm_last_click_tick = 0;
static int fm_last_click_idx = -1;
static char fm_status[64];
static uint64_t fm_status_until = 0;

/* New folder prompt (File Manager modal) */
static int fm_prompt_vis = 0;
static char fm_prompt_buf[64];
static int fm_prompt_len = 0;
static char fm_prompt_msg[64];
static uint64_t fm_prompt_msg_until = 0;

static void fm_set_status(const char *msg, uint32_t ticks) {
  if (!msg)
    msg = "";
  memset(fm_status, 0, sizeof(fm_status));
  strncpy(fm_status, msg, (int)sizeof(fm_status) - 1);
  fm_status_until = timer_get_ticks() + ticks;
}

static void fm_prompt_set_msg(const char *msg, uint32_t ticks) {
  if (!msg)
    msg = "";
  memset(fm_prompt_msg, 0, sizeof(fm_prompt_msg));
  strncpy(fm_prompt_msg, msg, (int)sizeof(fm_prompt_msg) - 1);
  fm_prompt_msg_until = timer_get_ticks() + ticks;
}

static void fm_prompt_open(void) {
  fm_prompt_vis = 1;
  memset(fm_prompt_buf, 0, sizeof(fm_prompt_buf));
  memset(fm_prompt_msg, 0, sizeof(fm_prompt_msg));
  fm_prompt_msg_until = 0;
  /* Sensible default to edit */
  strncpy(fm_prompt_buf, "new_folder", (int)sizeof(fm_prompt_buf) - 1);
  fm_prompt_len = (int)strlen(fm_prompt_buf);
}

static void fm_prompt_close(void) {
  fm_prompt_vis = 0;
  memset(fm_prompt_msg, 0, sizeof(fm_prompt_msg));
  fm_prompt_msg_until = 0;
}

static void fm_open_dir(fs_node_t *dir) {
  if (dir && dir->type == FS_DIRECTORY) {
    fm_dir = dir;
    fm_selected = -1;
  }
}

static void fm_init_if_needed(void) {
  if (!fm_dir)
    fm_dir = fs_get_root();
}

static int fm_make_child_path(char *out, int out_sz, fs_node_t *dir,
                              const char *name) {
  if (!out || out_sz < 2 || !dir || !name || !name[0])
    return 0;
  char base[FS_MAX_PATH];
  memset(base, 0, sizeof(base));
  fs_get_path(dir, base, sizeof(base));

  int pos = 0;
  /* base */
  for (int i = 0; base[i] && pos < out_sz - 1; i++)
    out[pos++] = base[i];
  if (pos == 0) {
    out[pos++] = '/';
  }
  /* slash if needed */
  if (pos > 1 && out[pos - 1] != '/' && pos < out_sz - 1)
    out[pos++] = '/';
  /* name */
  for (int i = 0; name[i] && pos < out_sz - 1; i++)
    out[pos++] = name[i];
  out[pos] = '\0';
  return 1;
}

static int fm_valid_new_name(const char *name) {
  if (!name || !name[0])
    return 0;
  if (!strcmp(name, ".") || !strcmp(name, ".."))
    return 0;
  for (int i = 0; name[i]; i++) {
    unsigned char c = (unsigned char)name[i];
    if (c < 32 || c == 127)
      return 0;
    if (c == '/' || c == '\\')
      return 0;
  }
  return 1;
}

static void fm_create_folder_named(const char *name) {
  fm_init_if_needed();
  if (!fm_dir || fm_dir->type != FS_DIRECTORY)
    return;

  char tmp[64];
  memset(tmp, 0, sizeof(tmp));
  if (!name)
    name = "";
  strncpy(tmp, name, (int)sizeof(tmp) - 1);
  /* Trim spaces */
  while (tmp[0] == ' ')
    memmove(tmp, tmp + 1, strlen(tmp));
  int n = (int)strlen(tmp);
  while (n > 0 && tmp[n - 1] == ' ') {
    tmp[n - 1] = '\0';
    n--;
  }

  if (!fm_valid_new_name(tmp)) {
    fm_prompt_set_msg("Invalid name", 180);
    return;
  }

  char path[FS_MAX_PATH];
  memset(path, 0, sizeof(path));
  if (!fm_make_child_path(path, sizeof(path), fm_dir, tmp)) {
    fm_prompt_set_msg("Name too long", 180);
    return;
  }

  if (fs_create_dir(path)) {
    fm_set_status("Folder created", 120);
    fm_prompt_close();
  } else {
    fm_prompt_set_msg("Create failed (exists?)", 180);
  }
}

static void fm_prompt_handle_key(char c) {
  if (!fm_prompt_vis)
    return;

  if (c == '\n') {
    fm_create_folder_named(fm_prompt_buf);
    return;
  }
  if ((unsigned char)c == 27) { /* Esc */
    fm_prompt_close();
    return;
  }
  if (c == '\b') {
    if (fm_prompt_len > 0) {
      fm_prompt_len--;
      fm_prompt_buf[fm_prompt_len] = '\0';
    }
    return;
  }
  if ((unsigned char)c >= 0x80)
    return;
  if (c >= 32 && c <= 126 && fm_prompt_len < (int)sizeof(fm_prompt_buf) - 1) {
    fm_prompt_buf[fm_prompt_len++] = c;
    fm_prompt_buf[fm_prompt_len] = '\0';
  }
}

static void fm_delete_selected(void) {
  fm_init_if_needed();
  if (!fm_dir)
    return;
  if (fm_selected < 0 || fm_selected >= fm_dir->child_count) {
    fm_set_status("Select an item", 120);
    return;
  }
  fs_node_t *n = fm_dir->children[fm_selected];
  if (!n) {
    fm_set_status("Invalid item", 120);
    return;
  }
  char path[FS_MAX_PATH];
  memset(path, 0, sizeof(path));
  fs_get_path(n, path, sizeof(path));
  if (fs_remove(path) == 0) {
    fm_selected = -1;
    fm_set_status("Deleted", 120);
  } else {
    fm_set_status("Delete failed", 180);
  }
}

static void fm_open_in_terminal(void) {
  fm_init_if_needed();
  fs_node_t *target = fm_dir;
  if (fm_dir && fm_selected >= 0 && fm_selected < fm_dir->child_count) {
    fs_node_t *n = fm_dir->children[fm_selected];
    if (n && n->type == FS_DIRECTORY)
      target = n;
  }
  if (target)
    fs_set_cwd(target);

  win_vis = 1;
  win_minimized = 0;
  active_window_focus = 1;

  char pbuf[FS_MAX_PATH];
  memset(pbuf, 0, sizeof(pbuf));
  fs_get_path(fs_get_cwd(), pbuf, sizeof(pbuf));
  gui_term_set_color(0x7aa2f7);
  gui_term_print("[File Manager] cwd = ");
  gui_term_set_color(0xa9b1d6);
  gui_term_print(pbuf);
  gui_term_print("\n");
}

static void draw_files_window(void) {
  if (!files_vis || files_minimized)
    return;

  fm_init_if_needed();

  /* Shadow */
  fb_fill_rect(fx + 4, fy + 4, fw, fh, 0x050505);

  /* Background */
  fb_fill_rect(fx, fy, fw, fh, 0x1a1b26);
  fb_draw_rect(fx, fy, fw, fh, 0x3d3d5c);

  /* Title bar */
  fb_fill_rect(fx, fy, fw, TITLE_H, 0x3d3d5c);
  fb_draw_string(fx + 8, fy + 7, "File Manager", 0xFFFFFF, 0x3d3d5c, 1);

  /* Window buttons: [ - ] [ + ] [ x ] */
  int bclose = fx + fw - (BTN_PAD + BTN_SZ);
  int bmax = bclose - (BTN_SZ + 4);
  int bmin = bmax - (BTN_SZ + 4);
  fb_fill_rect(bclose, fy + BTN_PAD, BTN_SZ, BTN_SZ, 0xf7768e);
  fb_draw_char(bclose + 2, fy + 7, 'x', 0xFFFFFF, 0xf7768e, 1);
  fb_fill_rect(bmax, fy + BTN_PAD, BTN_SZ, BTN_SZ, 0x9ece6a);
  fb_draw_char(bmax + 2, fy + 7, '+', 0xFFFFFF, 0x9ece6a, 1);
  fb_fill_rect(bmin, fy + BTN_PAD, BTN_SZ, BTN_SZ, 0xe0af68);
  fb_draw_char(bmin + 2, fy + 7, '-', 0xFFFFFF, 0xe0af68, 1);

  /* Toolbar */
  int tx = fx + 10;
  int ty = fy + TITLE_H + 8;
  fb_fill_rect(fx + 1, fy + TITLE_H, fw - 2, 28, 0x16161f);
  fb_draw_hline(fx + 1, fy + TITLE_H + 27, fw - 2, 0x30363d);

  /* Up button */
  uint32_t ubg = (fm_dir && fm_dir != fs_get_root()) ? 0x3d3d5c : 0x24283b;
  uint32_t ufg = (fm_dir && fm_dir != fs_get_root()) ? 0xFFFFFF : 0x565f89;
  fb_fill_rect(tx, ty, 38, 18, ubg);
  fb_draw_rect(tx, ty, 38, 18, 0x565f89);
  fb_draw_string(tx + 12, ty + 5, "Up", ufg, ubg, 1);

  /* Action buttons */
  int abx = tx + 46;
  uint32_t abg = 0x3d3d5c;
  fb_fill_rect(abx, ty, 90, 18, abg);
  fb_draw_rect(abx, ty, 90, 18, 0x565f89);
  fb_draw_string(abx + 12, ty + 5, "New Folder", 0x9ece6a, abg, 1);

  int dbx = abx + 98;
  int can_del = (fm_dir && fm_selected >= 0 && fm_selected < fm_dir->child_count);
  uint32_t dbg = can_del ? 0x3d3d5c : 0x24283b;
  uint32_t dfg = can_del ? 0xf7768e : 0x565f89;
  fb_fill_rect(dbx, ty, 62, 18, dbg);
  fb_draw_rect(dbx, ty, 62, 18, 0x565f89);
  fb_draw_string(dbx + 10, ty + 5, "Delete", dfg, dbg, 1);

  int tbx = dbx + 70;
  fb_fill_rect(tbx, ty, 74, 18, 0x3d3d5c);
  fb_draw_rect(tbx, ty, 74, 18, 0x565f89);
  fb_draw_string(tbx + 10, ty + 5, "Terminal", 0x7dcfff, 0x3d3d5c, 1);

  /* Path */
  char pbuf[FS_MAX_PATH];
  memset(pbuf, 0, sizeof(pbuf));
  fs_get_path(fm_dir, pbuf, sizeof(pbuf));
  int path_x = tbx + 82;
  int path_w = fw - 20 - (path_x - fx);
  fb_fill_rect(path_x, ty, path_w, 18, 0x24283b);
  fb_draw_rect(path_x, ty, path_w, 18, 0x30363d);
  draw_string_ellipsis(path_x + 8, ty + 5, pbuf, 0xAAAAAA, 0x24283b, 1,
                       path_w - 16);

  if (fm_status[0] && timer_get_ticks() < fm_status_until) {
    draw_string_ellipsis(path_x + 8, ty + 22, fm_status, 0xe0af68, 0x1a1b26, 1,
                         fw - 20);
  }

  /* Split panes */
  int cx = fx + 10;
  int cy = fy + TITLE_H + 36;
  int ch = fh - (TITLE_H + 46);
  int list_w = (fw * 55) / 100;
  if (list_w < 240)
    list_w = 240;
  if (list_w > fw - 180)
    list_w = fw - 180;
  int prev_x = cx + list_w + 10;
  int prev_w = fx + fw - 10 - prev_x;
  if (prev_w < 140) {
    /* If too narrow, just show list */
    prev_w = 0;
  }

  fb_fill_rect(cx, cy, list_w, ch, 0x16161f);
  fb_draw_rect(cx, cy, list_w, ch, 0x30363d);
  if (prev_w > 0) {
    fb_fill_rect(prev_x, cy, prev_w, ch, 0x16161f);
    fb_draw_rect(prev_x, cy, prev_w, ch, 0x30363d);
  }

  /* List header */
  fb_draw_string(cx + 8, cy + 6, "Name", 0xAAAAAA, 0x16161f, 1);
  fb_draw_hline(cx + 6, cy + 18, list_w - 12, 0x30363d);

  int row_h = 14;
  int y = cy + 24;

  /* Optional parent row */
  int row = 0;
  if (fm_dir && fm_dir != fs_get_root()) {
    uint32_t bg = (fm_selected == -2) ? 0x24283b : 0x16161f;
    if (fm_selected == -2)
      fb_fill_rect(cx + 2, y - 1, list_w - 4, row_h, bg);
    fb_draw_string(cx + 10, y, "..", 0x7dcfff, bg, 1);
    y += row_h;
    row++;
  }

  int shown = 0;
  int max_rows = (ch - 30) / row_h;
  if (max_rows < 1)
    max_rows = 1;

  for (int i = 0; fm_dir && i < fm_dir->child_count && row < max_rows; i++) {
    fs_node_t *n = fm_dir->children[i];
    uint32_t bg = (fm_selected == i) ? 0x24283b : 0x16161f;
    if (fm_selected == i)
      fb_fill_rect(cx + 2, y - 1, list_w - 4, row_h, bg);
    char label[FS_MAX_NAME + 6];
    memset(label, 0, sizeof(label));
    if (n->type == FS_DIRECTORY)
      strcat(label, "[D] ");
    else
      strcat(label, "[F] ");
    strcat(label, n->name);
    draw_string_ellipsis(cx + 10, y, label,
                         (n->type == FS_DIRECTORY) ? 0x9ece6a : 0xFFFFFF, bg, 1,
                         list_w - 20);
    y += row_h;
    row++;
    shown++;
  }

  /* Preview */
  if (prev_w > 0) {
    fb_draw_string(prev_x + 8, cy + 6, "Preview", 0xAAAAAA, 0x16161f, 1);
    fb_draw_hline(prev_x + 6, cy + 18, prev_w - 12, 0x30363d);

    if (fm_selected >= 0 && fm_dir && fm_selected < fm_dir->child_count) {
      fs_node_t *sel = fm_dir->children[fm_selected];
      fb_draw_string(prev_x + 10, cy + 28, sel->name, 0xFFFFFF, 0x16161f, 1);

      if (sel->type == FS_DIRECTORY) {
        char s[32];
        int_to_str(sel->child_count, s);
        strcat(s, " items");
        fb_draw_string(prev_x + 10, cy + 44, s, 0xAAAAAA, 0x16161f, 1);
      } else {
        char s[32];
        int_to_str(sel->size, s);
        strcat(s, " bytes");
        fb_draw_string(prev_x + 10, cy + 44, s, 0xAAAAAA, 0x16161f, 1);

        /* Simple content preview (first lines) */
        int py = cy + 66;
        int max_py = cy + ch - 10;
        const char *c = sel->content;
        int line = 0;
        while (c && *c && py < max_py && line < 12) {
          char linebuf[64];
          memset(linebuf, 0, sizeof(linebuf));
          int j = 0;
          while (*c && *c != '\n' && j < (int)sizeof(linebuf) - 1) {
            char chh = *c++;
            if ((unsigned char)chh < 32)
              chh = ' ';
            linebuf[j++] = chh;
          }
          if (*c == '\n')
            c++;
          draw_string_ellipsis(prev_x + 10, py, linebuf, 0xAAAAAA, 0x16161f, 1,
                               prev_w - 20);
          py += 14;
          line++;
        }
      }
    } else {
      fb_draw_string(prev_x + 10, cy + 40, "Select an item to preview.",
                     0x565f89, 0x16161f, 1);
    }
  }

  /* Resize grip */
  if (!files_maximized) {
    fb_fill_rect(fx + fw - RESIZE_GRIP, fy + fh - RESIZE_GRIP, RESIZE_GRIP,
                 RESIZE_GRIP, 0x24283b);
    fb_draw_char(fx + fw - 10, fy + fh - 10, '\\', 0x565f89, 0x24283b, 1);
  }

  /* New folder modal prompt */
  if (fm_prompt_vis) {
    int mw = 320;
    int mh = 120;
    if (mw > fw - 40)
      mw = fw - 40;
    if (mw < 220)
      mw = 220;

    int mx0 = fx + (fw - mw) / 2;
    int my0 = fy + TITLE_H + 40;
    if (my0 + mh > fy + fh - 20)
      my0 = fy + fh - mh - 20;

    fb_fill_rect(mx0 + 4, my0 + 4, mw, mh, 0x050505);
    fb_fill_rect(mx0, my0, mw, mh, 0x1e1e2e);
    fb_draw_rect(mx0, my0, mw, mh, 0x565f89);
    fb_fill_rect(mx0, my0, mw, 22, 0x2a2a4a);
    fb_draw_string(mx0 + 8, my0 + 7, "New Folder", 0xFFFFFF, 0x2a2a4a, 1);

    fb_draw_string(mx0 + 10, my0 + 34, "Name:", 0xAAAAAA, 0x1e1e2e, 1);
    int ibx = mx0 + 60;
    int iby = my0 + 30;
    int ibw = mw - 70;
    fb_fill_rect(ibx, iby, ibw, 20, 0x24283b);
    fb_draw_rect(ibx, iby, ibw, 20, 0x30363d);
    draw_string_ellipsis(ibx + 6, iby + 6, fm_prompt_buf, 0xFFFFFF, 0x24283b, 1,
                         ibw - 12);

    /* caret */
    int caret_x = ibx + 6 + fm_prompt_len * CHAR_W;
    if (caret_x > ibx + ibw - 8)
      caret_x = ibx + ibw - 8;
    fb_fill_rect(caret_x, iby + 4, 2, 12, 0xa9b1d6);

    /* OK / Cancel */
    int by = my0 + mh - 28;
    int okx = mx0 + mw - 150;
    int cancel_x = mx0 + mw - 74;
    fb_fill_rect(okx, by, 70, 20, 0x3d3d5c);
    fb_draw_rect(okx, by, 70, 20, 0x565f89);
    fb_draw_string(okx + 26, by + 6, "OK", 0x9ece6a, 0x3d3d5c, 1);
    fb_fill_rect(cancel_x, by, 70, 20, 0x3d3d5c);
    fb_draw_rect(cancel_x, by, 70, 20, 0x565f89);
    fb_draw_string(cancel_x + 10, by + 6, "Cancel", 0xf7768e, 0x3d3d5c, 1);

    uint64_t now = timer_get_ticks();
    if (fm_prompt_msg[0] && now < fm_prompt_msg_until) {
      draw_string_ellipsis(mx0 + 10, my0 + 58, fm_prompt_msg, 0xe0af68, 0x1e1e2e,
                           1, mw - 20);
    } else {
      fb_draw_string(mx0 + 10, my0 + 58, "Enter to create, Esc to cancel",
                     0x565f89, 0x1e1e2e, 1);
    }
  }
}

/* ============================================================
 * Calculator Window
 * ============================================================ */
static char calc_entry[64] = "0";
static int calc_entry_len = 1;
static long long calc_acc = 0;
static int calc_has_acc = 0;
static char calc_op = 0;      /* '+', '-', '*', '/' */
static int calc_new_entry = 1; /* next digit starts a new entry */
static int calc_error = 0;

static void calc_reset_all(void) {
  memset(calc_entry, 0, sizeof(calc_entry));
  strcpy(calc_entry, "0");
  calc_entry_len = 1;
  calc_acc = 0;
  calc_has_acc = 0;
  calc_op = 0;
  calc_new_entry = 1;
  calc_error = 0;
}

static long long calc_parse_ll(const char *s) {
  if (!s || !*s)
    return 0;
  int neg = 0;
  int i = 0;
  if (s[0] == '-') {
    neg = 1;
    i = 1;
  }
  long long v = 0;
  for (; s[i]; i++) {
    char c = s[i];
    if (c < '0' || c > '9')
      break;
    v = v * 10 + (long long)(c - '0');
  }
  return neg ? -v : v;
}

static void calc_ll_to_str(long long v, char *out, int out_sz) {
  if (!out || out_sz <= 1)
    return;
  char tmp[32];
  int pos = 0;
  int neg = 0;
  if (v == 0) {
    out[0] = '0';
    out[1] = '\0';
    return;
  }
  if (v < 0) {
    neg = 1;
    v = -v;
  }
  while (v > 0 && pos < (int)sizeof(tmp) - 1) {
    tmp[pos++] = (char)('0' + (v % 10));
    v /= 10;
  }
  if (neg && pos < (int)sizeof(tmp) - 1)
    tmp[pos++] = '-';
  int j = 0;
  while (pos > 0 && j < out_sz - 1)
    out[j++] = tmp[--pos];
  out[j] = '\0';
}

static int calc_apply_op(long long a, long long b, char op, long long *out) {
  if (!out)
    return 0;
  switch (op) {
  case '+':
    *out = a + b;
    return 1;
  case '-':
    *out = a - b;
    return 1;
  case '*':
    *out = a * b;
    return 1;
  case '/':
    if (b == 0)
      return 0;
    *out = a / b;
    return 1;
  default:
    *out = b;
    return 1;
  }
}

static void calc_input_digit(char d) {
  if (calc_error)
    calc_reset_all();

  if (calc_new_entry) {
    memset(calc_entry, 0, sizeof(calc_entry));
    calc_entry[0] = d;
    calc_entry[1] = '\0';
    calc_entry_len = 1;
    calc_new_entry = 0;
    return;
  }

  if (calc_entry_len < (int)sizeof(calc_entry) - 1) {
    if (calc_entry_len == 1 && calc_entry[0] == '0') {
      calc_entry[0] = d;
      return;
    }
    calc_entry[calc_entry_len++] = d;
    calc_entry[calc_entry_len] = '\0';
  }
}

static void calc_backspace(void) {
  if (calc_error) {
    calc_reset_all();
    return;
  }
  if (calc_new_entry) {
    /* backspace on a fresh entry does nothing */
    return;
  }
  if (calc_entry_len > 1) {
    calc_entry_len--;
    calc_entry[calc_entry_len] = '\0';
  } else {
    strcpy(calc_entry, "0");
    calc_entry_len = 1;
    calc_new_entry = 1;
  }
}

static void calc_toggle_sign(void) {
  if (calc_error) {
    calc_reset_all();
    return;
  }
  if (calc_entry[0] == '-') {
    memmove(calc_entry, calc_entry + 1, strlen(calc_entry));
    calc_entry_len = (int)strlen(calc_entry);
    if (calc_entry_len <= 0) {
      strcpy(calc_entry, "0");
      calc_entry_len = 1;
    }
  } else if (!(calc_entry_len == 1 && calc_entry[0] == '0')) {
    if (calc_entry_len < (int)sizeof(calc_entry) - 1) {
      memmove(calc_entry + 1, calc_entry, strlen(calc_entry) + 1);
      calc_entry[0] = '-';
      calc_entry_len++;
    }
  }
}

static void calc_press_op(char op) {
  if (calc_error) {
    calc_reset_all();
  }

  long long b = calc_parse_ll(calc_entry);
  if (!calc_has_acc) {
    calc_acc = b;
    calc_has_acc = 1;
  } else if (calc_op) {
    long long r = 0;
    if (!calc_apply_op(calc_acc, b, calc_op, &r)) {
      calc_error = 1;
      strcpy(calc_entry, "Error");
      calc_entry_len = (int)strlen(calc_entry);
      calc_new_entry = 1;
      return;
    }
    calc_acc = r;
    calc_ll_to_str(calc_acc, calc_entry, (int)sizeof(calc_entry));
    calc_entry_len = (int)strlen(calc_entry);
  }

  calc_op = op;
  calc_new_entry = 1;
}

static void calc_equals(void) {
  if (calc_error) {
    calc_reset_all();
    return;
  }
  if (!calc_has_acc) {
    calc_new_entry = 1;
    return;
  }
  long long b = calc_parse_ll(calc_entry);
  long long r = 0;
  if (!calc_apply_op(calc_acc, b, calc_op, &r)) {
    calc_error = 1;
    strcpy(calc_entry, "Error");
    calc_entry_len = (int)strlen(calc_entry);
    calc_new_entry = 1;
    return;
  }
  calc_acc = r;
  calc_ll_to_str(calc_acc, calc_entry, (int)sizeof(calc_entry));
  calc_entry_len = (int)strlen(calc_entry);
  calc_op = 0;
  calc_has_acc = 0;
  calc_new_entry = 1;
}

static void calc_clear_entry(void) {
  if (calc_error) {
    calc_reset_all();
    return;
  }
  strcpy(calc_entry, "0");
  calc_entry_len = 1;
  calc_new_entry = 1;
}

static void calc_handle_key(char c) {
  if (!calc_vis || calc_minimized || active_window_focus != 8)
    return;
  if (c >= '0' && c <= '9') {
    calc_input_digit(c);
    return;
  }
  if (c == '\b') {
    calc_backspace();
    return;
  }
  if (c == '\n') {
    calc_equals();
    return;
  }
  if (c == '+')
    calc_press_op('+');
  else if (c == '-')
    calc_press_op('-');
  else if (c == '*')
    calc_press_op('*');
  else if (c == '/')
    calc_press_op('/');
  else if (c == 'c' || c == 'C')
    calc_reset_all();
}

static void draw_calc_window(void) {
  if (!calc_vis || calc_minimized)
    return;

  /* Shadow */
  fb_fill_rect(calcx + 4, calcy + 4, calcw, calch, 0x050505);

  /* Background */
  fb_fill_rect(calcx, calcy, calcw, calch, 0x1a1b26);
  fb_draw_rect(calcx, calcy, calcw, calch, 0x3d3d5c);

  /* Title bar */
  fb_fill_rect(calcx, calcy, calcw, TITLE_H, 0x3d3d5c);
  fb_draw_string(calcx + 8, calcy + 7, "Calculator", 0xFFFFFF, 0x3d3d5c, 1);

  /* Window buttons: [ - ] [ + ] [ x ] */
  int bclose = calcx + calcw - (BTN_PAD + BTN_SZ);
  int bmax = bclose - (BTN_SZ + 4);
  int bmin = bmax - (BTN_SZ + 4);
  fb_fill_rect(bclose, calcy + BTN_PAD, BTN_SZ, BTN_SZ, 0xf7768e);
  fb_draw_char(bclose + 2, calcy + 7, 'x', 0xFFFFFF, 0xf7768e, 1);
  fb_fill_rect(bmax, calcy + BTN_PAD, BTN_SZ, BTN_SZ, 0x9ece6a);
  fb_draw_char(bmax + 2, calcy + 7, '+', 0xFFFFFF, 0x9ece6a, 1);
  fb_fill_rect(bmin, calcy + BTN_PAD, BTN_SZ, BTN_SZ, 0xe0af68);
  fb_draw_char(bmin + 2, calcy + 7, '-', 0xFFFFFF, 0xe0af68, 1);

  /* Display */
  int pad = 10;
  int disp_h = 42;
  int dx = calcx + pad;
  int dy = calcy + TITLE_H + pad;
  int dw = calcw - pad * 2;
  fb_fill_rect(dx, dy, dw, disp_h, 0x16161f);
  fb_draw_rect(dx, dy, dw, disp_h, 0x30363d);
  draw_string_ellipsis(dx + 8, dy + 16, calc_entry, 0xFFFFFF, 0x16161f, 1,
                       dw - 16);

  /* Buttons grid */
  int grid_x = dx;
  int grid_y = dy + disp_h + 10;
  int grid_w = dw;
  int grid_h = calch - (grid_y - calcy) - 12;
  if (grid_h < 140)
    grid_h = 140;

  int cols = 4;
  int rows = 5;
  int gap = 6;
  int btn_w = (grid_w - gap * (cols - 1)) / cols;
  int btn_h = (grid_h - gap * (rows - 1)) / rows;
  if (btn_h > 42)
    btn_h = 42;
  if (btn_w < 40)
    btn_w = 40;

  const char *labels[20] = {"7", "8", "9", "/",
                            "4", "5", "6", "*",
                            "1", "2", "3", "-",
                            "0", "+/-", "C", "+",
                            "BS", "CE", "=", ""};

  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      int idx = r * cols + c;
      const char *lab = labels[idx];
      if (!lab || !lab[0])
        continue;
      int bx0 = grid_x + c * (btn_w + gap);
      int by0 = grid_y + r * (btn_h + gap);
      uint32_t bg = 0x24283b;
      uint32_t fg = 0xFFFFFF;
      if (!strcmp(lab, "+") || !strcmp(lab, "-") || !strcmp(lab, "*") ||
          !strcmp(lab, "/") || !strcmp(lab, "=")) {
        bg = 0x3d3d5c;
      }
      if (!strcmp(lab, "C") || !strcmp(lab, "CE")) {
        fg = 0xf7768e;
      }
      fb_fill_rect(bx0, by0, btn_w, btn_h, bg);
      fb_draw_rect(bx0, by0, btn_w, btn_h, 0x565f89);
      int lw = (int)strlen(lab) * CHAR_W;
      int tx = bx0 + (btn_w - lw) / 2;
      int ty = by0 + (btn_h - CHAR_H) / 2;
      fb_draw_string(tx, ty, lab, fg, bg, 1);
    }
  }

  /* Resize grip */
  if (!calc_maximized) {
    fb_fill_rect(calcx + calcw - RESIZE_GRIP, calcy + calch - RESIZE_GRIP,
                 RESIZE_GRIP, RESIZE_GRIP, 0x24283b);
    fb_draw_char(calcx + calcw - 10, calcy + calch - 10, '\\', 0x565f89,
                 0x24283b, 1);
  }
}

/* ============================================================
 * Notepad Window
 * ============================================================ */
#define NOTE_MAX FS_MAX_CONTENT
static char note_buf[NOTE_MAX];
static int note_len = 0;
static int note_cur = 0; /* cursor index into note_buf */
static int note_scroll_line = 0;
static char note_path[FS_MAX_PATH];
static char note_status[64];
static uint64_t note_status_until = 0;

/* Open/Save As prompt */
static int note_prompt_vis = 0;
static int note_prompt_mode = 0; /* 1=open, 2=saveas */
static char note_prompt_buf[FS_MAX_PATH];
static int note_prompt_len = 0;
static char note_prompt_msg[64];
static uint64_t note_prompt_msg_until = 0;

#define NOTE_TOOLBAR_H 26

static void note_reset(void) {
  memset(note_buf, 0, sizeof(note_buf));
  note_len = 0;
  note_cur = 0;
  note_scroll_line = 0;
  memset(note_path, 0, sizeof(note_path));
  memset(note_status, 0, sizeof(note_status));
  note_status_until = 0;
  note_prompt_vis = 0;
  note_prompt_mode = 0;
  memset(note_prompt_buf, 0, sizeof(note_prompt_buf));
  note_prompt_len = 0;
  memset(note_prompt_msg, 0, sizeof(note_prompt_msg));
  note_prompt_msg_until = 0;
}

static void note_set_status(const char *msg, uint32_t ticks) {
  if (!msg)
    msg = "";
  memset(note_status, 0, sizeof(note_status));
  strncpy(note_status, msg, (int)sizeof(note_status) - 1);
  note_status_until = timer_get_ticks() + ticks;
}

static void note_prompt_set_msg(const char *msg, uint32_t ticks) {
  if (!msg)
    msg = "";
  memset(note_prompt_msg, 0, sizeof(note_prompt_msg));
  strncpy(note_prompt_msg, msg, (int)sizeof(note_prompt_msg) - 1);
  note_prompt_msg_until = timer_get_ticks() + ticks;
}

static void note_prompt_open(int mode) {
  note_prompt_vis = 1;
  note_prompt_mode = mode;
  memset(note_prompt_msg, 0, sizeof(note_prompt_msg));
  note_prompt_msg_until = 0;
  memset(note_prompt_buf, 0, sizeof(note_prompt_buf));
  if (note_path[0]) {
    strncpy(note_prompt_buf, note_path, (int)sizeof(note_prompt_buf) - 1);
  } else {
    /* default into cwd */
    char cwdp[FS_MAX_PATH];
    memset(cwdp, 0, sizeof(cwdp));
    fs_get_path(fs_get_cwd(), cwdp, sizeof(cwdp));
    if (strcmp(cwdp, "/") == 0)
      strncpy(note_prompt_buf, "/note.txt", (int)sizeof(note_prompt_buf) - 1);
    else {
      strncpy(note_prompt_buf, cwdp, (int)sizeof(note_prompt_buf) - 1);
      int l = (int)strlen(note_prompt_buf);
      if (l < (int)sizeof(note_prompt_buf) - 1 && note_prompt_buf[l - 1] != '/')
        strcat(note_prompt_buf, "/");
      strcat(note_prompt_buf, "note.txt");
    }
  }
  note_prompt_len = (int)strlen(note_prompt_buf);
}

static void note_prompt_close(void) {
  note_prompt_vis = 0;
  note_prompt_mode = 0;
  memset(note_prompt_msg, 0, sizeof(note_prompt_msg));
  note_prompt_msg_until = 0;
}

static int note_open_path(const char *path) {
  if (!path || !path[0])
    return 0;
  fs_node_t *n = fs_resolve_path(path);
  if (!n || n->type != FS_FILE)
    return 0;
  int len = n->size;
  if (len < 0)
    len = 0;
  if (len > NOTE_MAX - 1)
    len = NOTE_MAX - 1;
  memset(note_buf, 0, sizeof(note_buf));
  memcpy(note_buf, n->content, (size_t)len);
  note_buf[len] = '\0';
  note_len = len;
  note_cur = 0;
  note_scroll_line = 0;
  memset(note_path, 0, sizeof(note_path));
  strncpy(note_path, path, (int)sizeof(note_path) - 1);
  note_set_status("Opened", 120);
  return 1;
}

static int note_save_path(const char *path) {
  if (!path || !path[0])
    return 0;
  fs_node_t *n = fs_resolve_path(path);
  if (!n) {
    n = fs_create_file(path);
  }
  if (!n || n->type != FS_FILE)
    return 0;
  int len = note_len;
  if (len > NOTE_MAX - 1)
    len = NOTE_MAX - 1;
  if (fs_write(n, note_buf, len) == 0) {
    memset(note_path, 0, sizeof(note_path));
    strncpy(note_path, path, (int)sizeof(note_path) - 1);
    note_set_status("Saved", 120);
    return 1;
  }
  return 0;
}

static void note_prompt_handle_key(char c) {
  if (!note_prompt_vis)
    return;

  if (c == '\n') {
    if (note_prompt_mode == 1) {
      if (!note_open_path(note_prompt_buf))
        note_prompt_set_msg("Open failed", 180);
      else
        note_prompt_close();
    } else if (note_prompt_mode == 2) {
      if (!note_save_path(note_prompt_buf))
        note_prompt_set_msg("Save failed", 180);
      else
        note_prompt_close();
    }
    return;
  }
  if ((unsigned char)c == 27) { /* Esc */
    note_prompt_close();
    return;
  }
  if (c == '\b') {
    if (note_prompt_len > 0) {
      note_prompt_len--;
      note_prompt_buf[note_prompt_len] = '\0';
    }
    return;
  }
  if ((unsigned char)c >= 0x80)
    return;
  if (c >= 32 && c <= 126 &&
      note_prompt_len < (int)sizeof(note_prompt_buf) - 1) {
    note_prompt_buf[note_prompt_len++] = c;
    note_prompt_buf[note_prompt_len] = '\0';
  }
}

static void note_insert_char(char c) {
  if (note_len >= NOTE_MAX - 1)
    return;
  if (note_cur < 0)
    note_cur = 0;
  if (note_cur > note_len)
    note_cur = note_len;
  memmove(note_buf + note_cur + 1, note_buf + note_cur, (size_t)(note_len - note_cur));
  note_buf[note_cur] = c;
  note_len++;
  note_buf[note_len] = '\0';
  note_cur++;
}

static void note_backspace(void) {
  if (note_cur <= 0 || note_len <= 0)
    return;
  memmove(note_buf + note_cur - 1, note_buf + note_cur, (size_t)(note_len - note_cur));
  note_len--;
  note_cur--;
  if (note_len < 0)
    note_len = 0;
  note_buf[note_len] = '\0';
}

static void note_delete_at_cursor(void) {
  if (note_cur < 0 || note_cur >= note_len || note_len <= 0)
    return;
  memmove(note_buf + note_cur, note_buf + note_cur + 1,
          (size_t)(note_len - note_cur - 1));
  note_len--;
  note_buf[note_len] = '\0';
}

static void note_move_left(void) {
  if (note_cur > 0)
    note_cur--;
}

static void note_move_right(void) {
  if (note_cur < note_len)
    note_cur++;
}

static void note_move_home(void) {
  while (note_cur > 0 && note_buf[note_cur - 1] != '\n')
    note_cur--;
}

static void note_move_end(void) {
  while (note_cur < note_len && note_buf[note_cur] != '\n')
    note_cur++;
}

static int note_index_from_xy(int target_line, int target_col, int cols);

static void note_compute_cursor_line_col(int cols, int *out_line, int *out_col) {
  if (cols < 1)
    cols = 1;
  int line = 0;
  int col = 0;
  for (int i = 0; i < note_cur && i < note_len; i++) {
    char ch = note_buf[i];
    if (ch == '\n') {
      line++;
      col = 0;
    } else {
      col++;
      if (col >= cols) {
        line++;
        col = 0;
      }
    }
  }
  if (out_line)
    *out_line = line;
  if (out_col)
    *out_col = col;
}

static void note_move_up_down(int dir, int cols) {
  int line = 0, col = 0;
  note_compute_cursor_line_col(cols, &line, &col);
  int tline = line + dir;
  if (tline < 0)
    tline = 0;
  int idx = note_index_from_xy(tline, col, cols);
  if (idx < 0)
    idx = 0;
  if (idx > note_len)
    idx = note_len;
  note_cur = idx;
}

static void note_handle_key(char c) {
  if (!note_vis || note_minimized || active_window_focus != 9)
    return;
  if (note_prompt_vis)
    return;

  /* Compute editor columns for movement (wrap-aware). */
  int pad = 10;
  int bw0 = notew - pad * 2;
  int cols = bw0 / CHAR_W;
  if (cols < 1)
    cols = 1;

  /* Special key codes (see keyboard.h) */
  if ((unsigned char)c == KEY_LEFT) { note_move_left(); return; }
  if ((unsigned char)c == KEY_RIGHT) { note_move_right(); return; }
  if ((unsigned char)c == KEY_UP) { note_move_up_down(-1, cols); return; }
  if ((unsigned char)c == KEY_DOWN) { note_move_up_down(1, cols); return; }
  if ((unsigned char)c == KEY_HOME) { note_move_home(); return; }
  if ((unsigned char)c == KEY_END) { note_move_end(); return; }
  if ((unsigned char)c == KEY_DELETE) { note_delete_at_cursor(); return; }

  if (c == '\n') {
    note_insert_char('\n');
    return;
  }
  if (c == '\b') {
    note_backspace();
    return;
  }
  if (c == '\t') {
    /* Tabs as 4 spaces */
    for (int i = 0; i < 4; i++)
      note_insert_char(' ');
    return;
  }
  if ((unsigned char)c >= 0x80)
    return;
  if (c >= 32 && c <= 126) {
    note_insert_char(c);
  }
}

static int note_compute_cursor_line(int cols) {
  if (cols < 1)
    cols = 1;
  int line = 0;
  int col = 0;
  for (int i = 0; i < note_cur && i < note_len; i++) {
    char ch = note_buf[i];
    if (ch == '\n') {
      line++;
      col = 0;
    } else {
      col++;
      if (col >= cols) {
        line++;
        col = 0;
      }
    }
  }
  return line;
}

static void note_scroll_keep_cursor_visible(int cols, int rows) {
  int cur_line = note_compute_cursor_line(cols);
  if (rows < 1)
    rows = 1;
  if (cur_line < note_scroll_line)
    note_scroll_line = cur_line;
  if (cur_line >= note_scroll_line + rows)
    note_scroll_line = cur_line - rows + 1;
  if (note_scroll_line < 0)
    note_scroll_line = 0;
}

static int note_index_from_xy(int target_line, int target_col, int cols) {
  if (cols < 1)
    cols = 1;
  if (target_line < 0)
    target_line = 0;
  if (target_col < 0)
    target_col = 0;

  int line = 0;
  int col = 0;
  for (int i = 0; i < note_len; i++) {
    if (line > target_line)
      return i;
    if (line == target_line && col >= target_col)
      return i;

    char ch = note_buf[i];
    if (ch == '\n') {
      line++;
      col = 0;
    } else {
      col++;
      if (col >= cols) {
        line++;
        col = 0;
      }
    }
  }
  return note_len;
}

static void draw_notepad_window(void) {
  if (!note_vis || note_minimized)
    return;

  /* Shadow */
  fb_fill_rect(notex + 4, notey + 4, notew, noteh, 0x050505);

  /* Background */
  fb_fill_rect(notex, notey, notew, noteh, 0x1a1b26);
  fb_draw_rect(notex, notey, notew, noteh, 0x3d3d5c);

  /* Title bar */
  fb_fill_rect(notex, notey, notew, TITLE_H, 0x3d3d5c);
  fb_draw_string(notex + 8, notey + 7, "Notepad", 0xFFFFFF, 0x3d3d5c, 1);

  /* Window buttons: [ - ] [ + ] [ x ] */
  int bclose = notex + notew - (BTN_PAD + BTN_SZ);
  int bmax = bclose - (BTN_SZ + 4);
  int bmin = bmax - (BTN_SZ + 4);
  fb_fill_rect(bclose, notey + BTN_PAD, BTN_SZ, BTN_SZ, 0xf7768e);
  fb_draw_char(bclose + 2, notey + 7, 'x', 0xFFFFFF, 0xf7768e, 1);
  fb_fill_rect(bmax, notey + BTN_PAD, BTN_SZ, BTN_SZ, 0x9ece6a);
  fb_draw_char(bmax + 2, notey + 7, '+', 0xFFFFFF, 0x9ece6a, 1);
  fb_fill_rect(bmin, notey + BTN_PAD, BTN_SZ, BTN_SZ, 0xe0af68);
  fb_draw_char(bmin + 2, notey + 7, '-', 0xFFFFFF, 0xe0af68, 1);

  /* Toolbar */
  int pad = 10;
  int tx = notex + pad;
  int ty = notey + TITLE_H + 8;
  fb_fill_rect(notex + 1, notey + TITLE_H, notew - 2, NOTE_TOOLBAR_H + 10,
               0x16161f);
  fb_draw_hline(notex + 1, notey + TITLE_H + NOTE_TOOLBAR_H + 9, notew - 2,
                0x30363d);

  int bx_btn = tx;
  const int btn_h = 18;
  const int gap = 6;
  /* New */
  fb_fill_rect(bx_btn, ty, 44, btn_h, 0x3d3d5c);
  fb_draw_rect(bx_btn, ty, 44, btn_h, 0x565f89);
  fb_draw_string(bx_btn + 10, ty + 5, "New", 0xFFFFFF, 0x3d3d5c, 1);
  bx_btn += 44 + gap;
  /* Open */
  fb_fill_rect(bx_btn, ty, 52, btn_h, 0x3d3d5c);
  fb_draw_rect(bx_btn, ty, 52, btn_h, 0x565f89);
  fb_draw_string(bx_btn + 10, ty + 5, "Open", 0x7dcfff, 0x3d3d5c, 1);
  bx_btn += 52 + gap;
  /* Save */
  fb_fill_rect(bx_btn, ty, 52, btn_h, 0x3d3d5c);
  fb_draw_rect(bx_btn, ty, 52, btn_h, 0x565f89);
  fb_draw_string(bx_btn + 10, ty + 5, "Save", 0x9ece6a, 0x3d3d5c, 1);
  bx_btn += 52 + gap;
  /* Save As */
  fb_fill_rect(bx_btn, ty, 72, btn_h, 0x3d3d5c);
  fb_draw_rect(bx_btn, ty, 72, btn_h, 0x565f89);
  fb_draw_string(bx_btn + 10, ty + 5, "Save As", 0xe0af68, 0x3d3d5c, 1);

  /* Path display */
  int path_x = tx + 44 + gap + 52 + gap + 52 + gap + 72 + 10;
  int path_w = notex + notew - pad - path_x;
  if (path_w > 40) {
    fb_fill_rect(path_x, ty, path_w, btn_h, 0x24283b);
    fb_draw_rect(path_x, ty, path_w, btn_h, 0x30363d);
    draw_string_ellipsis(path_x + 6, ty + 5, note_path[0] ? note_path : "(untitled)",
                         0xAAAAAA, 0x24283b, 1, path_w - 12);
  }

  uint64_t now = timer_get_ticks();
  if (note_status[0] && now < note_status_until) {
    draw_string_ellipsis(tx, ty + 22, note_status, 0xe0af68, 0x1a1b26, 1,
                         notew - pad * 2);
  }

  /* Editor body */
  int bx0 = notex + pad;
  int by0 = notey + TITLE_H + pad + NOTE_TOOLBAR_H + 14;
  int bw0 = notew - pad * 2;
  int bh0 = noteh - TITLE_H - pad * 2 - (NOTE_TOOLBAR_H + 14);
  if (bw0 < 40)
    bw0 = 40;
  if (bh0 < 40)
    bh0 = 40;
  fb_fill_rect(bx0, by0, bw0, bh0, 0x16161f);
  fb_draw_rect(bx0, by0, bw0, bh0, 0x30363d);

  int cols = bw0 / CHAR_W;
  int rows = bh0 / CHAR_H;
  if (cols < 1)
    cols = 1;
  if (rows < 1)
    rows = 1;

  /* Keep cursor visible as the user types. */
  note_scroll_keep_cursor_visible(cols, rows);

  /* Render text with wrapping + scrolling by logical lines. */
  int line = 0;
  int col = 0;
  int draw_line = 0;

  int cursor_line = 0;
  int cursor_col = 0;
  /* compute cursor position while scanning */
  for (int i = 0; i <= note_len; i++) {
    if (i == note_cur) {
      cursor_line = line;
      cursor_col = col;
    }
    if (i == note_len)
      break;
    char ch = note_buf[i];
    if (ch == '\n') {
      line++;
      col = 0;
      continue;
    }
    col++;
    if (col >= cols) {
      line++;
      col = 0;
    }
  }

  line = 0;
  col = 0;
  for (int i = 0; i < note_len; i++) {
    char ch = note_buf[i];
    if (ch == '\n') {
      line++;
      col = 0;
      continue;
    }

    if (line >= note_scroll_line && draw_line < rows) {
      int sx = bx0 + col * CHAR_W;
      int sy = by0 + (line - note_scroll_line) * CHAR_H;
      if (ch >= 32 && ch <= 126)
        fb_draw_char_nobg(sx, sy, ch, 0xc0caf5, 1);
    }

    col++;
    if (col >= cols) {
      line++;
      col = 0;
    }

    if (line >= note_scroll_line)
      draw_line = line - note_scroll_line;
    if (draw_line >= rows)
      break;
  }

  /* Cursor blink */
  if ((timer_get_ticks() / 50) % 2 == 0) {
    int cx = bx0 + cursor_col * CHAR_W;
    int cy = by0 + (cursor_line - note_scroll_line) * CHAR_H;
    if (cursor_line >= note_scroll_line && cursor_line < note_scroll_line + rows) {
      fb_fill_rect(cx, cy, CHAR_W, CHAR_H, 0xa9b1d6);
    }
  }

  /* Resize grip */
  if (!note_maximized) {
    fb_fill_rect(notex + notew - RESIZE_GRIP, notey + noteh - RESIZE_GRIP,
                 RESIZE_GRIP, RESIZE_GRIP, 0x24283b);
    fb_draw_char(notex + notew - 10, notey + noteh - 10, '\\', 0x565f89,
                 0x24283b, 1);
  }

  /* Open/SaveAs modal prompt */
  if (note_prompt_vis) {
    int mw = 360;
    int mh = 120;
    if (mw > notew - 40)
      mw = notew - 40;
    if (mw < 240)
      mw = 240;
    int mx0 = notex + (notew - mw) / 2;
    int my0 = notey + TITLE_H + 60;
    if (my0 + mh > notey + noteh - 20)
      my0 = notey + noteh - mh - 20;

    fb_fill_rect(mx0 + 4, my0 + 4, mw, mh, 0x050505);
    fb_fill_rect(mx0, my0, mw, mh, 0x1e1e2e);
    fb_draw_rect(mx0, my0, mw, mh, 0x565f89);
    fb_fill_rect(mx0, my0, mw, 22, 0x2a2a4a);
    fb_draw_string(mx0 + 8, my0 + 7,
                   (note_prompt_mode == 1) ? "Open File" : "Save File",
                   0xFFFFFF, 0x2a2a4a, 1);

    fb_draw_string(mx0 + 10, my0 + 34, "Path:", 0xAAAAAA, 0x1e1e2e, 1);
    int ibx = mx0 + 56;
    int iby = my0 + 30;
    int ibw = mw - 66;
    fb_fill_rect(ibx, iby, ibw, 20, 0x24283b);
    fb_draw_rect(ibx, iby, ibw, 20, 0x30363d);
    draw_string_ellipsis(ibx + 6, iby + 6, note_prompt_buf, 0xFFFFFF, 0x24283b, 1,
                         ibw - 12);

    /* caret */
    int caret_x = ibx + 6 + note_prompt_len * CHAR_W;
    if (caret_x > ibx + ibw - 8)
      caret_x = ibx + ibw - 8;
    fb_fill_rect(caret_x, iby + 4, 2, 12, 0xa9b1d6);

    int by = my0 + mh - 28;
    int okx = mx0 + mw - 150;
    int cancel_x = mx0 + mw - 74;
    fb_fill_rect(okx, by, 70, 20, 0x3d3d5c);
    fb_draw_rect(okx, by, 70, 20, 0x565f89);
    fb_draw_string(okx + 26, by + 6, "OK", 0x9ece6a, 0x3d3d5c, 1);
    fb_fill_rect(cancel_x, by, 70, 20, 0x3d3d5c);
    fb_draw_rect(cancel_x, by, 70, 20, 0x565f89);
    fb_draw_string(cancel_x + 10, by + 6, "Cancel", 0xf7768e, 0x3d3d5c, 1);

    if (note_prompt_msg[0] && now < note_prompt_msg_until) {
      draw_string_ellipsis(mx0 + 10, my0 + 58, note_prompt_msg, 0xe0af68, 0x1e1e2e,
                           1, mw - 20);
    } else {
      fb_draw_string(mx0 + 10, my0 + 58, "Enter to confirm, Esc to cancel",
                     0x565f89, 0x1e1e2e, 1);
    }
  }
}

/* ============================================================
 * Web Browser Window (Minimal, Local-File Text Browser)
 * ============================================================ */
#define BR_MAX_TEXT 2048
#define BR_MAX_LINKS 12
#define BR_HIST_MAX 16

typedef struct {
  char href[FS_MAX_PATH];
  char label[64];
} br_link_t;

static char br_url[FS_MAX_PATH];
static char br_addr[FS_MAX_PATH];
static int br_addr_len = 0;
static int br_editing = 0;
static char br_status[64];
static uint64_t br_status_until = 0;
static int br_scroll = 0;
static char br_text[BR_MAX_TEXT];
static char br_http_buf[BR_MAX_TEXT];
static br_link_t br_links[BR_MAX_LINKS];
static int br_link_count = 0;
static char br_hist[BR_HIST_MAX][FS_MAX_PATH];
static int br_hist_count = 0;
static int br_hist_pos = -1;

/* Runtime coords for click handling */
static int br_links_x = 0, br_links_y = 0, br_links_w = 0, br_links_h = 0;
static int br_links_row_h = 14;

static void br_set_status(const char *msg, uint32_t ticks) {
  if (!msg)
    msg = "";
  memset(br_status, 0, sizeof(br_status));
  strncpy(br_status, msg, (int)sizeof(br_status) - 1);
  br_status_until = timer_get_ticks() + ticks;
}

static void br_text_clear(void) {
  memset(br_text, 0, sizeof(br_text));
}

static void br_links_clear(void) {
  for (int i = 0; i < BR_MAX_LINKS; i++) {
    memset(br_links[i].href, 0, sizeof(br_links[i].href));
    memset(br_links[i].label, 0, sizeof(br_links[i].label));
  }
  br_link_count = 0;
}

static void br_append_char(char c, int *pos) {
  if (!pos)
    return;
  if (*pos >= (int)sizeof(br_text) - 1)
    return;
  br_text[*pos] = c;
  (*pos)++;
  br_text[*pos] = '\0';
}

static void br_append_str(const char *s, int *pos) {
  if (!s || !pos)
    return;
  while (*s)
    br_append_char(*s++, pos);
}

static int br_is_tag_start(const char *p) {
  return p && p[0] == '<';
}

static void br_decode_entity(const char **pp, int *pos) {
  const char *p = pp ? *pp : 0;
  if (!p || p[0] != '&') {
    if (pp)
      *pp = p;
    return;
  }
  if (starts_with(p, "&lt;")) {
    br_append_char('<', pos);
    p += 4;
  } else if (starts_with(p, "&gt;")) {
    br_append_char('>', pos);
    p += 4;
  } else if (starts_with(p, "&amp;")) {
    br_append_char('&', pos);
    p += 5;
  } else if (starts_with(p, "&quot;")) {
    br_append_char('"', pos);
    p += 6;
  } else {
    /* Unknown: emit '&' and move on */
    br_append_char('&', pos);
    p += 1;
  }
  if (pp)
    *pp = p;
}

static void br_render_html_to_text(const char *src) {
  br_text_clear();
  br_links_clear();
  br_scroll = 0;

  if (!src) {
    strcpy(br_text, "(empty)\n");
    return;
  }

  int out = 0;
  const char *p = src;
  while (*p) {
    if (*p == '&') {
      br_decode_entity(&p, &out);
      continue;
    }

    if (br_is_tag_start(p)) {
      /* Simple tag parser */
      const char *tag = p + 1;
      int is_end = 0;
      if (*tag == '/') {
        is_end = 1;
        tag++;
      }

      /* Capture tag name */
      char name[8];
      memset(name, 0, sizeof(name));
      int ni = 0;
      while (*tag && *tag != '>' && *tag != ' ' && ni < (int)sizeof(name) - 1) {
        char c = *tag++;
        if (c >= 'A' && c <= 'Z')
          c = (char)(c - 'A' + 'a');
        name[ni++] = c;
      }
      name[ni] = '\0';

      /* Anchor handling: <a href="...">text</a> */
      if (!is_end && name[0] == 'a') {
        const char *href = 0;
        const char *scan = p;
        while (*scan && *scan != '>') {
          if ((scan[0] == 'h' || scan[0] == 'H') &&
              (scan[1] == 'r' || scan[1] == 'R') &&
              (scan[2] == 'e' || scan[2] == 'E') &&
              (scan[3] == 'f' || scan[3] == 'F') && scan[4] == '=') {
            scan += 5;
            if (*scan == '"' || *scan == '\'') {
              char q = *scan++;
              href = scan;
              while (*scan && *scan != q)
                scan++;
            } else {
              href = scan;
              while (*scan && *scan != ' ' && *scan != '>')
                scan++;
            }
            break;
          }
          scan++;
        }

        /* Advance to '>' */
        while (*p && *p != '>')
          p++;
        if (*p == '>')
          p++;

        /* Capture inner text until </a> (very simple) */
        char label[64];
        memset(label, 0, sizeof(label));
        int li = 0;
        const char *t = p;
        while (*t) {
          if (t[0] == '<' && t[1] == '/' &&
              (t[2] == 'a' || t[2] == 'A'))
            break;
          if (*t == '<') {
            /* Skip nested tags */
            while (*t && *t != '>')
              t++;
            if (*t == '>')
              t++;
            continue;
          }
          if (*t == '&') {
            char tmp[8];
            memset(tmp, 0, sizeof(tmp));
            /* Decode into tmp by using br_decode_entity logic in a sandbox */
            /* (Keep it minimal: only the 4 entities above) */
            if (starts_with(t, "&lt;")) { tmp[0] = '<'; t += 4; }
            else if (starts_with(t, "&gt;")) { tmp[0] = '>'; t += 4; }
            else if (starts_with(t, "&amp;")) { tmp[0] = '&'; t += 5; }
            else if (starts_with(t, "&quot;")) { tmp[0] = '"'; t += 6; }
            else { tmp[0] = '&'; t += 1; }
            if (li < (int)sizeof(label) - 1)
              label[li++] = tmp[0];
            continue;
          }
          if ((unsigned char)*t >= 32 && (unsigned char)*t <= 126) {
            if (li < (int)sizeof(label) - 1)
              label[li++] = *t;
          } else if (*t == '\n' || *t == '\t') {
            if (li < (int)sizeof(label) - 1)
              label[li++] = ' ';
          }
          t++;
        }
        label[li] = '\0';

        /* Store link */
        if (href && br_link_count < BR_MAX_LINKS) {
          int href_len = 0;
          const char *hs = href;
          /* href points into src; copy until quote/space/> */
          while (*hs && *hs != '"' && *hs != '\'' && *hs != ' ' && *hs != '>' &&
                 href_len < FS_MAX_PATH - 1) {
            br_links[br_link_count].href[href_len++] = *hs++;
          }
          br_links[br_link_count].href[href_len] = '\0';
          strncpy(br_links[br_link_count].label, label,
                  (int)sizeof(br_links[br_link_count].label) - 1);
          br_link_count++;
        }

        /* Emit label with index marker */
        br_append_str(label[0] ? label : "link", &out);
        if (br_link_count > 0) {
          br_append_str(" [", &out);
          char nbuf[8];
          memset(nbuf, 0, sizeof(nbuf));
          int_to_str(br_link_count, nbuf);
          br_append_str(nbuf, &out);
          br_append_str("]", &out);
        }
        continue;
      }

      /* Newline-ish tags */
      if (!strcmp(name, "br") || !strcmp(name, "p") || !strcmp(name, "h1") ||
          !strcmp(name, "h2") || !strcmp(name, "li") || !strcmp(name, "div") ||
          !strcmp(name, "tr") || (!strcmp(name, "p") && is_end)) {
        br_append_char('\n', &out);
      }

      /* Skip tag to '>' */
      while (*p && *p != '>')
        p++;
      if (*p == '>')
        p++;
      continue;
    }

    char c = *p++;
    if (c == '\r')
      continue;
    if (c == '\n') {
      br_append_char('\n', &out);
      continue;
    }
    if (c == '\t')
      c = ' ';
    if ((unsigned char)c < 32 || (unsigned char)c > 126)
      c = ' ';
    br_append_char(c, &out);
  }

  /* Ensure trailing newline */
  int l = (int)strlen(br_text);
  if (l == 0 || br_text[l - 1] != '\n') {
    int out2 = l;
    br_append_char('\n', &out2);
  }
}

static void br_hist_push(const char *url) {
  if (!url || !*url)
    return;
  /* truncate forward history */
  if (br_hist_pos >= 0 && br_hist_pos < br_hist_count - 1) {
    br_hist_count = br_hist_pos + 1;
  }
  if (br_hist_count < BR_HIST_MAX) {
    strncpy(br_hist[br_hist_count], url, FS_MAX_PATH - 1);
    br_hist[br_hist_count][FS_MAX_PATH - 1] = '\0';
    br_hist_count++;
    br_hist_pos = br_hist_count - 1;
  } else {
    for (int i = 0; i < BR_HIST_MAX - 1; i++)
      strcpy(br_hist[i], br_hist[i + 1]);
    strncpy(br_hist[BR_HIST_MAX - 1], url, FS_MAX_PATH - 1);
    br_hist[BR_HIST_MAX - 1][FS_MAX_PATH - 1] = '\0';
    br_hist_pos = BR_HIST_MAX - 1;
    br_hist_count = BR_HIST_MAX;
  }
}

static void br_load_url_internal(const char *url, int push_hist) {
  if (!url || !*url)
    return;

  if (push_hist)
    br_hist_push(url);

  memset(br_url, 0, sizeof(br_url));
  strncpy(br_url, url, (int)sizeof(br_url) - 1);
  memset(br_addr, 0, sizeof(br_addr));
  strncpy(br_addr, url, (int)sizeof(br_addr) - 1);
  br_addr_len = (int)strlen(br_addr);
  br_editing = 0;

  if (starts_with(url, "https://")) {
    br_render_html_to_text(
        "akaOS Browser\n\nHTTPS (TLS) is not supported yet.\n"
        "Try http://<ip>/ or local files.\n");
    br_set_status("No HTTPS", 180);
    return;
  }
  if (starts_with(url, "http://")) {
    if (!net_is_available()) {
      br_render_html_to_text("Network not available.\n");
      br_set_status("Network down", 180);
      return;
    }

    const char *p = url + 7;
    char host[128];
    memset(host, 0, sizeof(host));
    int hi = 0;
    while (*p && *p != '/' && *p != ':' && hi < (int)sizeof(host) - 1)
      host[hi++] = *p++;
    host[hi] = '\0';

    int port = 80;
    if (*p == ':') {
      p++;
      int v = 0;
      while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        p++;
      }
      if (v > 0 && v < 65536)
        port = v;
    }

    const char *path = (*p == '/') ? p : "/";

    /* Parse IPv4 dotted-quad (DNS is not implemented yet). */
    uint8_t oct[4] = {0, 0, 0, 0};
    int oi = 0;
    int acc = 0;
    int ok = 1;
    for (int i = 0; host[i]; i++) {
      char c = host[i];
      if (c == '.') {
        if (oi >= 3) { ok = 0; break; }
        if (acc < 0 || acc > 255) { ok = 0; break; }
        oct[oi++] = (uint8_t)acc;
        acc = 0;
        continue;
      }
      if (c >= '0' && c <= '9') {
        acc = acc * 10 + (c - '0');
        if (acc > 255) { ok = 0; break; }
      } else {
        ok = 0;
        break;
      }
    }
    if (ok) {
      if (oi != 3 || acc < 0 || acc > 255)
        ok = 0;
      else
        oct[3] = (uint8_t)acc;
    }
    if (!ok) {
      br_render_html_to_text(
          "DNS is not implemented yet.\n\n"
          "Use an IP address for now, e.g.:\n"
          "  http://93.184.216.34/\n");
      br_set_status("No DNS", 180);
      return;
    }

    uint32_t ip = net_make_ip(oct[0], oct[1], oct[2], oct[3]);
    memset(br_http_buf, 0, sizeof(br_http_buf));
    int n = net_http_get(ip, port, host, path, br_http_buf, (int)sizeof(br_http_buf),
                         5000);
    if (n <= 0) {
      br_render_html_to_text("HTTP request failed.\n");
      br_set_status("HTTP failed", 180);
      return;
    }

    /* Strip headers */
    const char *body = br_http_buf;
    for (int i = 0; br_http_buf[i]; i++) {
      if (br_http_buf[i] == '\r' && br_http_buf[i+1] == '\n' &&
          br_http_buf[i+2] == '\r' && br_http_buf[i+3] == '\n') {
        body = &br_http_buf[i + 4];
        break;
      }
    }
    br_render_html_to_text(body);
    br_set_status("Loaded", 120);
    return;
  }

  if (starts_with(url, "about:")) {
    if (strcmp(url, "about:home") == 0 || strcmp(url, "about:") == 0) {
      br_render_html_to_text(
          "akaOS Browser (about:home)\n\n"
          "This is a minimal text browser.\n\n"
          "Open local files:\n"
          "  /home/root/readme.txt\n"
          "  file:/home/root/page.html\n\n"
          "Links:\n"
          "<a href=\"about:help\">Help</a>\n");
    } else if (strcmp(url, "about:help") == 0) {
      br_render_html_to_text(
          "Help\n\n"
          "Controls:\n"
          "  Click address bar to type\n"
          "  Enter = Go\n"
          "  g = edit address\n"
          "  Up/Down/PgUp/PgDn = scroll\n"
          "  1..9 = open link\n\n"
          "<a href=\"about:home\">Home</a>\n");
    } else {
      br_render_html_to_text("about page not found\n");
    }
    return;
  }

  const char *path = url;
  if (starts_with(url, "file:"))
    path = url + 5;

  fs_node_t *n = fs_resolve_path(path);
  if (!n || n->type != FS_FILE) {
    br_render_html_to_text("Not found.\n");
    br_set_status("Not found", 180);
    return;
  }

  br_render_html_to_text(n->content);
}

static void br_init(void) {
  memset(br_url, 0, sizeof(br_url));
  strcpy(br_url, "about:home");
  memset(br_addr, 0, sizeof(br_addr));
  strcpy(br_addr, "about:home");
  br_addr_len = (int)strlen(br_addr);
  br_editing = 0;
  memset(br_status, 0, sizeof(br_status));
  br_status_until = 0;
  br_scroll = 0;
  br_hist_count = 0;
  br_hist_pos = -1;
  br_links_clear();
  br_render_html_to_text(0);
  br_load_url_internal("about:home", 1);
}

static void br_back(void) {
  if (br_hist_pos > 0) {
    br_hist_pos--;
    br_load_url_internal(br_hist[br_hist_pos], 0);
  }
}

static void br_forward(void) {
  if (br_hist_pos >= 0 && br_hist_pos < br_hist_count - 1) {
    br_hist_pos++;
    br_load_url_internal(br_hist[br_hist_pos], 0);
  }
}

static void br_go_addr(void) {
  if (br_addr[0])
    br_load_url_internal(br_addr, 1);
}

static void br_open_link(int idx1) {
  int idx = idx1 - 1;
  if (idx < 0 || idx >= br_link_count)
    return;
  if (br_links[idx].href[0])
    br_load_url_internal(br_links[idx].href, 1);
}

static int br_count_lines(void) {
  int lines = 0;
  for (int i = 0; br_text[i]; i++)
    if (br_text[i] == '\n')
      lines++;
  return (lines < 1) ? 1 : lines;
}

static void br_scroll_by(int delta, int rows) {
  int total = br_count_lines();
  int max_scroll = total - rows;
  if (max_scroll < 0)
    max_scroll = 0;
  br_scroll += delta;
  if (br_scroll < 0)
    br_scroll = 0;
  if (br_scroll > max_scroll)
    br_scroll = max_scroll;
}

static void br_handle_key(char c) {
  if (!browser_vis || browser_minimized || active_window_focus != 10)
    return;

  if (br_editing) {
    if (c == '\n') {
      br_go_addr();
      return;
    }
    if ((unsigned char)c == 27) { /* Esc */
      br_editing = 0;
      return;
    }
    if (c == '\b') {
      if (br_addr_len > 0) {
        br_addr_len--;
        br_addr[br_addr_len] = '\0';
      }
      return;
    }
    if ((unsigned char)c >= 0x80)
      return;
    if (c >= 32 && c <= 126 && br_addr_len < FS_MAX_PATH - 1) {
      br_addr[br_addr_len++] = c;
      br_addr[br_addr_len] = '\0';
    }
    return;
  }

  if (c == 'g' || c == 'G') {
    br_editing = 1;
    return;
  }
  if ((unsigned char)c == KEY_UP) {
    br_scroll_by(-1, 24);
    return;
  }
  if ((unsigned char)c == KEY_DOWN) {
    br_scroll_by(1, 24);
    return;
  }
  if ((unsigned char)c == KEY_PGUP) {
    br_scroll_by(-10, 24);
    return;
  }
  if ((unsigned char)c == KEY_PGDN) {
    br_scroll_by(10, 24);
    return;
  }
  if (c >= '1' && c <= '9') {
    br_open_link(c - '0');
    return;
  }
}

static void draw_browser_window(void) {
  if (!browser_vis || browser_minimized)
    return;

  /* Shadow */
  fb_fill_rect(brx + 4, bry + 4, brw, brh, 0x050505);

  /* Background */
  fb_fill_rect(brx, bry, brw, brh, 0x1a1b26);
  fb_draw_rect(brx, bry, brw, brh, 0x3d3d5c);

  /* Title bar */
  fb_fill_rect(brx, bry, brw, TITLE_H, 0x3d3d5c);
  fb_draw_string(brx + 8, bry + 7, "Web Browser", 0xFFFFFF, 0x3d3d5c, 1);

  /* Window buttons: [ - ] [ + ] [ x ] */
  int bclose = brx + brw - (BTN_PAD + BTN_SZ);
  int bmax = bclose - (BTN_SZ + 4);
  int bmin = bmax - (BTN_SZ + 4);
  fb_fill_rect(bclose, bry + BTN_PAD, BTN_SZ, BTN_SZ, 0xf7768e);
  fb_draw_char(bclose + 2, bry + 7, 'x', 0xFFFFFF, 0xf7768e, 1);
  fb_fill_rect(bmax, bry + BTN_PAD, BTN_SZ, BTN_SZ, 0x9ece6a);
  fb_draw_char(bmax + 2, bry + 7, '+', 0xFFFFFF, 0x9ece6a, 1);
  fb_fill_rect(bmin, bry + BTN_PAD, BTN_SZ, BTN_SZ, 0xe0af68);
  fb_draw_char(bmin + 2, bry + 7, '-', 0xFFFFFF, 0xe0af68, 1);

  /* Toolbar */
  int pad = 10;
  int tx = brx + pad;
  int ty = bry + TITLE_H + 8;
  fb_fill_rect(brx + 1, bry + TITLE_H, brw - 2, 36, 0x16161f);
  fb_draw_hline(brx + 1, bry + TITLE_H + 35, brw - 2, 0x30363d);

  /* Buttons */
  uint32_t bg = 0x3d3d5c;
  fb_fill_rect(tx, ty, 44, 18, bg);
  fb_draw_rect(tx, ty, 44, 18, 0x565f89);
  fb_draw_string(tx + 10, ty + 5, "<-", 0xFFFFFF, bg, 1);
  fb_fill_rect(tx + 50, ty, 44, 18, bg);
  fb_draw_rect(tx + 50, ty, 44, 18, 0x565f89);
  fb_draw_string(tx + 60, ty + 5, "->", 0xFFFFFF, bg, 1);
  fb_fill_rect(tx + 100, ty, 36, 18, bg);
  fb_draw_rect(tx + 100, ty, 36, 18, 0x565f89);
  fb_draw_string(tx + 110, ty + 5, "Go", 0x9ece6a, bg, 1);

  int addr_x = tx + 144;
  int addr_w = brw - pad * 2 - 144;
  if (addr_w < 60)
    addr_w = 60;
  fb_fill_rect(addr_x, ty, addr_w, 18, 0x24283b);
  fb_draw_rect(addr_x, ty, addr_w, 18, 0x30363d);
  draw_string_ellipsis(addr_x + 6, ty + 5, br_addr, 0xFFFFFF, 0x24283b, 1,
                       addr_w - 12);

  if (br_editing && (timer_get_ticks() / 30) % 2 == 0) {
    int caret_x = addr_x + 6 + br_addr_len * CHAR_W;
    if (caret_x > addr_x + addr_w - 8)
      caret_x = addr_x + addr_w - 8;
    fb_fill_rect(caret_x, ty + 4, 2, 12, 0xa9b1d6);
  }

  uint64_t now = timer_get_ticks();
  if (br_status[0] && now < br_status_until) {
    draw_string_ellipsis(tx, ty + 22, br_status, 0xe0af68, 0x1a1b26, 1,
                         brw - pad * 2);
  }

  /* Content + links */
  int cx = brx + pad;
  int cy = bry + TITLE_H + 44;
  int cw = brw - pad * 2;
  int ch = brh - (cy - bry) - 12;
  if (cw < 80)
    cw = 80;
  if (ch < 80)
    ch = 80;

  int link_h = ch / 3;
  if (link_h > 120)
    link_h = 120;
  if (link_h < 72)
    link_h = 72;
  int text_h = ch - link_h - 8;
  if (text_h < 40)
    text_h = 40;

  fb_fill_rect(cx, cy, cw, text_h, 0x16161f);
  fb_draw_rect(cx, cy, cw, text_h, 0x30363d);

  int rows = text_h / CHAR_H;
  if (rows < 1)
    rows = 1;
  int cols = cw / CHAR_W;
  if (cols < 1)
    cols = 1;

  /* Render visible lines from br_text */
  int line = 0;
  int i = 0;
  while (br_text[i] && line < br_scroll) {
    if (br_text[i] == '\n')
      line++;
    i++;
  }
  for (int r = 0; r < rows; r++) {
    if (!br_text[i])
      break;
    char linebuf[256];
    memset(linebuf, 0, sizeof(linebuf));
    int li = 0;
    while (br_text[i] && br_text[i] != '\n' && li < (int)sizeof(linebuf) - 1) {
      linebuf[li++] = br_text[i++];
    }
    linebuf[li] = '\0';
    if (br_text[i] == '\n')
      i++;
    draw_string_ellipsis(cx + 6, cy + r * CHAR_H, linebuf, 0xc0caf5, 0x16161f,
                         1, cw - 12);
  }

  /* Links pane */
  int lx = cx;
  int ly = cy + text_h + 8;
  fb_fill_rect(lx, ly, cw, link_h, 0x16161f);
  fb_draw_rect(lx, ly, cw, link_h, 0x30363d);
  fb_draw_string(lx + 8, ly + 6, "Links", 0xAAAAAA, 0x16161f, 1);
  fb_draw_hline(lx + 6, ly + 18, cw - 12, 0x30363d);

  br_links_x = lx;
  br_links_y = ly + 22;
  br_links_w = cw;
  br_links_h = link_h - 24;
  br_links_row_h = 14;

  int max_links_rows = br_links_h / br_links_row_h;
  if (max_links_rows < 1)
    max_links_rows = 1;
  for (int k = 0; k < br_link_count && k < max_links_rows; k++) {
    char row[96];
    memset(row, 0, sizeof(row));
    char nbuf[8];
    memset(nbuf, 0, sizeof(nbuf));
    int_to_str(k + 1, nbuf);
    strcpy(row, "[");
    strcat(row, nbuf);
    strcat(row, "] ");
    /* Safe append without relying on strncat (not always available). */
    int rp = (int)strlen(row);
    for (int j = 0; br_links[k].label[j] && rp < (int)sizeof(row) - 1; j++)
      row[rp++] = br_links[k].label[j];
    row[rp] = '\0';
    draw_string_ellipsis(lx + 10, br_links_y + k * br_links_row_h, row, 0x7dcfff,
                         0x16161f, 1, cw - 20);
  }

  /* Resize grip */
  if (!browser_maximized) {
    fb_fill_rect(brx + brw - RESIZE_GRIP, bry + brh - RESIZE_GRIP, RESIZE_GRIP,
                 RESIZE_GRIP, 0x24283b);
    fb_draw_char(brx + brw - 10, bry + brh - 10, '\\', 0x565f89, 0x24283b, 1);
  }
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

static void draw_bar_graph_compact(int x, int y, int width, int percent,
                                   uint32_t bg, uint32_t fg) {
  fb_fill_rect(x, y, width, 12, bg);
  int fill_w = (width * percent) / 100;
  if (fill_w > 0) {
    uint32_t c = fg;
    if (percent > 60)
      c = 0xe0af68; /* Yellow */
    if (percent > 85)
      c = 0xf7768e; /* Red */
    fb_fill_rect(x, y, fill_w, 12, c);
  }
}

static void draw_string_clipped(int x, int y, const char *s, uint32_t fg,
                                uint32_t bg, int scale, int max_px) {
  if (!s || scale < 1 || max_px <= 0)
    return;
  int max_chars = max_px / (CHAR_W * scale);
  if (max_chars <= 0)
    return;
  int cx = x;
  for (int i = 0; s[i] && i < max_chars; i++) {
    fb_draw_char(cx, y, s[i], fg, bg, scale);
    cx += CHAR_W * scale;
  }
}

static void draw_string_ellipsis(int x, int y, const char *s, uint32_t fg,
                                 uint32_t bg, int scale, int max_px) {
  if (!s || scale < 1 || max_px <= 0)
    return;
  int max_chars = max_px / (CHAR_W * scale);
  if (max_chars <= 0)
    return;

  int len = (int)strlen(s);
  if (len <= max_chars) {
    draw_string_clipped(x, y, s, fg, bg, scale, max_px);
    return;
  }

  /* Need truncation: keep room for "..." when possible. */
  int keep = max_chars;
  if (keep > 3)
    keep -= 3;
  else
    keep = max_chars;

  int cx = x;
  for (int i = 0; i < keep; i++) {
    fb_draw_char(cx, y, s[i], fg, bg, scale);
    cx += CHAR_W * scale;
  }

  if (max_chars > 3) {
    fb_draw_char(cx, y, '.', fg, bg, scale);
    cx += CHAR_W * scale;
    fb_draw_char(cx, y, '.', fg, bg, scale);
    cx += CHAR_W * scale;
    fb_draw_char(cx, y, '.', fg, bg, scale);
  }
}

static void draw_sysmon_window(void) {
  if (!sysmon_vis || sysmon_minimized)
    return;

  /* Shadow */
  fb_fill_rect(sx + 3, sy + 3, sw, sh, 0x080808);

  /* Background (htop style dark) */
  fb_fill_rect(sx, sy, sw, sh, 0x1a1b26);
  fb_draw_rect(sx, sy, sw, sh, 0x3d3d5c);

  /* Title bar */
  fb_fill_rect(sx, sy, sw, TITLE_H, 0x3d3d5c);
  fb_draw_string(sx + 6, sy + 7, "System Monitor", 0xFFFFFF, 0x3d3d5c, 1);

  /* Window buttons: [ - ] [ + ] [ x ] */
  int bclose = sx + sw - (BTN_PAD + BTN_SZ);
  int bmax = bclose - (BTN_SZ + 4);
  int bmin = bmax - (BTN_SZ + 4);
  fb_fill_rect(bclose, sy + BTN_PAD, BTN_SZ, BTN_SZ, 0xf7768e);
  fb_draw_char(bclose + 2, sy + 7, 'x', 0xFFFFFF, 0xf7768e, 1);
  fb_fill_rect(bmax, sy + BTN_PAD, BTN_SZ, BTN_SZ, 0x9ece6a);
  fb_draw_char(bmax + 2, sy + 7, '+', 0xFFFFFF, 0x9ece6a, 1);
  fb_fill_rect(bmin, sy + BTN_PAD, BTN_SZ, BTN_SZ, 0xe0af68);
  fb_draw_char(bmin + 2, sy + 7, '-', 0xFFFFFF, 0xe0af68, 1);

  /* Tabs (Task Manager style) */
  const int TAB_H = 18;
  int tab_y = sy + TITLE_H;
  fb_fill_rect(sx + 1, tab_y, sw - 2, TAB_H, 0x16161f);
  fb_draw_hline(sx + 1, tab_y + TAB_H - 1, sw - 2, 0x30363d);

  int tab_x = sx + 8;
  int tab_w = 110;
  sysmon_tab_perf_x = tab_x;
  sysmon_tab_perf_y = tab_y;
  sysmon_tab_perf_w = tab_w;
  sysmon_tab_perf_h = TAB_H;
  sysmon_tab_proc_x = tab_x + tab_w + 6;
  sysmon_tab_proc_y = tab_y;
  sysmon_tab_proc_w = tab_w;
  sysmon_tab_proc_h = TAB_H;

  uint32_t tab_on_bg = 0x24283b;
  uint32_t tab_off_bg = 0x16161f;
  uint32_t tab_border = 0x3d3d5c;

  fb_fill_rect(sysmon_tab_perf_x, sysmon_tab_perf_y + 1, sysmon_tab_perf_w,
               TAB_H - 2, (sysmon_tab == 0) ? tab_on_bg : tab_off_bg);
  fb_draw_rect(sysmon_tab_perf_x, sysmon_tab_perf_y + 1, sysmon_tab_perf_w,
               TAB_H - 2, tab_border);
  fb_draw_string(sysmon_tab_perf_x + 10, sysmon_tab_perf_y + 5, "Performance",
                 (sysmon_tab == 0) ? 0xFFFFFF : 0xAAAAAA,
                 (sysmon_tab == 0) ? tab_on_bg : tab_off_bg, 1);

  fb_fill_rect(sysmon_tab_proc_x, sysmon_tab_proc_y + 1, sysmon_tab_proc_w,
               TAB_H - 2, (sysmon_tab == 1) ? tab_on_bg : tab_off_bg);
  fb_draw_rect(sysmon_tab_proc_x, sysmon_tab_proc_y + 1, sysmon_tab_proc_w,
               TAB_H - 2, tab_border);
  fb_draw_string(sysmon_tab_proc_x + 10, sysmon_tab_proc_y + 5, "Processes",
                 (sysmon_tab == 1) ? 0xFFFFFF : 0xAAAAAA,
                 (sysmon_tab == 1) ? tab_on_bg : tab_off_bg, 1);

  /* Get Metrics */
  struct sys_metrics m;
  sysmon_update(&m);
  sysmon_last_core_count = m.core_count;

  /* --- Content --- */
  int content_top = sy + TITLE_H + TAB_H + 10;
  int gy = content_top;
  int gx = sx + 10;
  int show_stats = (sw >= 380);

  sysmon_pager_vis = 0;
  sysmon_pager_step = 0;
  sysmon_pager_prev_x = sysmon_pager_prev_y = 0;
  sysmon_pager_next_x = sysmon_pager_next_y = 0;

  if (sysmon_tab == 0 && m.core_count <= 16) {
    sysmon_core_offset = 0;
    for (int i = 0; i < m.core_count; i++) {
      /* Core labels can be 2 digits ("10", "12", ...). The previous fixed
         offsets caused '[' to overwrite the second digit. */
      char c_str[8];
      int_to_str(i + 1, c_str);
      fb_draw_string(gx, gy, c_str, 0x7dcfff, 0x1a1b26, 1);

      int label_w = (int)strlen(c_str) * CHAR_W;
      int x_lbr = gx + label_w;
      int x_bar = x_lbr + 12;      /* matches old gx+20 when label_w==8 */
      int x_rbr = x_bar + 150 + 5; /* matches old gx+175 */

      fb_draw_char(x_lbr, gy, '[', 0xAAAAAA, 0x1a1b26, 1);
      draw_bar_graph(x_bar, gy, 150, m.cpu_load_percent[i], 0x24283b,
                     0x9ece6a);
      fb_draw_char(x_rbr, gy, ']', 0xAAAAAA, 0x1a1b26, 1);
      gy += 16;
    }
  } else if (sysmon_tab == 0) {
    /* Compact multi-column grid for large core counts (up to MAX_CORES). */
    int row_h = 14;
    /* Reserve right sidebar space for stats so the grid doesn't overlap it. */
    int grid_left = gx;
    int grid_right = sx + sw - (show_stats ? 160 : 10);
    if (grid_right < grid_left + 200)
      grid_right = sx + sw - 10;
    int cpu_area_w = grid_right - grid_left;
    int col_gap = 10;
    int reserved_bottom = 140; /* mem + header + a few process rows */
    int max_rows = (sh - (TITLE_H + 10) - reserved_bottom) / row_h;
    if (max_rows < 6)
      max_rows = 6;
    if (max_rows > 24)
      max_rows = 24;

    int cols = 1;
    while (cols < 4 && ((m.core_count + cols - 1) / cols) > max_rows)
      cols++;

    if (cols < 1)
      cols = 1;
    int col_w = (cpu_area_w - (cols - 1) * col_gap) / cols;
    if (col_w < 70) {
      cols = 1;
      col_w = cpu_area_w;
    }
    int visible = cols * max_rows;
    if (visible < 1)
      visible = 1;

    /* Keep offset aligned to page size and in range. */
    sysmon_pager_step = visible;
    if (sysmon_core_offset < 0)
      sysmon_core_offset = 0;
    if (sysmon_core_offset >= m.core_count)
      sysmon_core_offset = 0;
    sysmon_core_offset = (sysmon_core_offset / visible) * visible;

    int shown = 0;
    for (int idx = 0; idx < visible; idx++) {
      int core = sysmon_core_offset + idx;
      if (core >= m.core_count)
        break;
      int col = idx / max_rows;
      int row = idx % max_rows;
      int x0 = grid_left + col * (col_w + col_gap);
      int y0 = gy + row * row_h;

      /* Fixed-width (2 char) labels keep the grid aligned. */
      char c_str[4];
      int label_val = core + 1;
      if (label_val < 10) {
        c_str[0] = ' ';
        c_str[1] = (char)('0' + label_val);
        c_str[2] = '\0';
      } else {
        int_to_str(label_val, c_str);
      }
      fb_draw_string(x0, y0 + 1, c_str, 0x7dcfff, 0x1a1b26, 1);

      int label_w = 2 * CHAR_W;
      int bar_x = x0 + label_w + 6;
      int bar_w = col_w - (label_w + 6);
      if (bar_w < 20)
        bar_w = 20;
      if (bar_x + bar_w > x0 + col_w)
        bar_w = (x0 + col_w) - bar_x;
      if (bar_w < 6)
        bar_w = 6;
      draw_bar_graph_compact(bar_x, y0 + 1, bar_w, m.cpu_load_percent[core],
                             0x24283b, 0x9ece6a);
      shown++;
    }

    int rows_used = 0;
    if (shown > 0) {
      /* Column-major fill: visual height depends only on rows, not columns. */
      rows_used = (shown < max_rows) ? shown : max_rows;
    }

    /* Pager (only when not all cores fit). */
    if (m.core_count > visible) {
      sysmon_pager_vis = 1;

      int first = sysmon_core_offset + 1;
      int last = sysmon_core_offset + visible;
      if (last > m.core_count)
        last = m.core_count;

      char pg[48];
      memset(pg, 0, sizeof(pg));
      strcat(pg, "Cores ");
      char n1[12], n2[12], n3[12];
      int_to_str(first, n1);
      int_to_str(last, n2);
      int_to_str(m.core_count, n3);
      strcat(pg, n1);
      strcat(pg, "-");
      strcat(pg, n2);
      strcat(pg, "/");
      strcat(pg, n3);

      int pager_y = gy + rows_used * row_h + 4;
      fb_draw_string(gx, pager_y, pg, 0xAAAAAA, 0x1a1b26, 1);

      int bx_next = grid_right - sysmon_pager_btn_w;
      int bx_prev = bx_next - (sysmon_pager_btn_w + 6);
      int by = pager_y - 1;

      sysmon_pager_prev_x = bx_prev;
      sysmon_pager_prev_y = by;
      sysmon_pager_next_x = bx_next;
      sysmon_pager_next_y = by;

      uint32_t bbg = 0x3d3d5c;
      fb_fill_rect(bx_prev, by, sysmon_pager_btn_w, sysmon_pager_btn_h, bbg);
      fb_fill_rect(bx_next, by, sysmon_pager_btn_w, sysmon_pager_btn_h, bbg);
      fb_draw_rect(bx_prev, by, sysmon_pager_btn_w, sysmon_pager_btn_h,
                   0x565f89);
      fb_draw_rect(bx_next, by, sysmon_pager_btn_w, sysmon_pager_btn_h,
                   0x565f89);
      fb_draw_char(bx_prev + 4, by + 3, '<', 0xFFFFFF, bbg, 1);
      fb_draw_char(bx_next + 4, by + 3, '>', 0xFFFFFF, bbg, 1);

      gy = pager_y + 18;
    } else {
      gy = gy + rows_used * row_h + 8;
    }
  }

  /* Right Stats (only when there's enough width to avoid overlap) */
  if (show_stats) {
    char u_str[32];
    timer_format_uptime(u_str, 32);
    fb_draw_string(sx + sw - 140, sy + TITLE_H + TAB_H + 10, "Uptime:", 0xAAAAAA,
                   0x1a1b26, 1);
    fb_draw_string(sx + sw - 80, sy + TITLE_H + TAB_H + 10, u_str, 0xFFFFFF,
                   0x1a1b26, 1);

    fb_draw_string(sx + sw - 140, sy + TITLE_H + TAB_H + 30, "Tasks:", 0xAAAAAA,
                   0x1a1b26, 1);
    char t_str[8];
    int_to_str(m.proc_count, t_str);
    fb_draw_string(sx + sw - 80, sy + TITLE_H + TAB_H + 30, t_str, 0xFFFFFF,
                   0x1a1b26, 1);
  }

  if (sysmon_tab == 0) {
    gy += 8;
    int mem_pct =
        m.mem_total_kb > 0 ? (m.mem_used_kb * 100) / m.mem_total_kb : 0;
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
  }

  /* --- Process List (Processes tab only) --- */
  /* In the Processes tab, start below the right-side stats block to avoid
     overlap with the header/first rows on smaller windows. */
  int proc_top = show_stats ? (sy + TITLE_H + TAB_H + 55) : content_top;
  int py = (sysmon_tab == 1) ? (proc_top) : (gy + 40);
  if (sysmon_tab != 1)
    goto sysmon_done;

  /* Header */
  int content_left = sx + 10;
  int content_right = sx + sw - 10;
  int content_w = content_right - content_left;
  if (content_w < 0)
    content_w = 0;

  /* Prevent drawing process rows beyond the bottom of the window. */
  int list_bottom = sy + sh - 8;
  if (!sysmon_maximized)
    list_bottom -= RESIZE_GRIP;

  /* If there's not even room for the header row, skip the process list. */
  if (py + 16 > list_bottom)
    goto sysmon_done;

  /* Responsive columns: always show PID + S + COMMAND; add others if width
     allows. */
  int show_user = (content_w >= 260);
  int show_pri = (content_w >= 500);
  int show_cpu = (content_w >= 310);
  int show_mem = (content_w >= 360);
  int show_virt = (content_w >= 430);

  fb_fill_rect(sx + 4, py, sw - 8, 16, 0xbb9af7);
  char hdr[64];
  memset(hdr, 0, sizeof(hdr));
  strcat(hdr, "PID");
  if (show_user)
    strcat(hdr, " USER");
  if (show_pri)
    strcat(hdr, " PRI");
  strcat(hdr, " S");
  if (show_cpu)
    strcat(hdr, " CPU%");
  if (show_mem)
    strcat(hdr, " MEM%");
  if (show_virt)
    strcat(hdr, " VIRT");
  strcat(hdr, " COMMAND");
  draw_string_clipped(sx + 10, py + 4, hdr, 0x1a1b26, 0xbb9af7, 1, sw - 20);
  py += 20;

  sysmon_visible_pid_count = 0;
  sysmon_end_btn_enabled = 0;
  sysmon_proc_rows_y = py;
  sysmon_proc_rows_x1 = sx + 4;
  sysmon_proc_rows_x2 = sx + sw - 4;

  /* Column x positions: laid out left-to-right based on what's visible. */
  int x = content_left;
  int x_pid = x;
  x += 32;
  int x_user = x;
  if (show_user)
    x += 80;
  int x_pri = x;
  if (show_pri)
    x += 40;
  int x_state = x;
  x += 23;
  int x_cpu = x;
  if (show_cpu)
    x += 48;
  int x_mem = x;
  if (show_mem)
    x += 47;
  int x_virt = x;
  if (show_virt)
    x += 60;
  int x_cmd = x;

  /* Leave room for the End Task button at the bottom of the list. */
  sysmon_end_btn_x = sx + sw - 10 - sysmon_end_btn_w;
  sysmon_end_btn_y = list_bottom - sysmon_end_btn_h;
  int rows_bottom = list_bottom - (sysmon_end_btn_h + 6);
  if (rows_bottom < py)
    rows_bottom = py;

  int max_proc_rows = (rows_bottom - py) / sysmon_proc_rows_h;
  if (max_proc_rows < 0)
    max_proc_rows = 0;
  int rows_to_draw = m.proc_count;
  if (rows_to_draw > max_proc_rows)
    rows_to_draw = max_proc_rows;

  int selected_found = 0;
  for (int i = 0; i < rows_to_draw; i++) {
    struct sys_proc *p = &m.procs[i];
    sysmon_visible_pids[i] = p->pid;
    sysmon_visible_pid_count = i + 1;
    if (p->pid == sysmon_selected_pid)
      selected_found = 1;

    if (p->pid == sysmon_selected_pid) {
      fb_fill_rect(sx + 6, py - 1, sw - 12, 14, 0x24283b);
    }

    char pid_str[8];
    int_to_str(p->pid, pid_str);
    /* Pad PID to 4 chars so it stays readable in tight layouts. */
    char pid_pad[8];
    memset(pid_pad, ' ', 4);
    pid_pad[4] = '\0';
    int pid_len = (int)strlen(pid_str);
    if (pid_len > 4)
      pid_len = 4;
    memcpy(pid_pad + (4 - pid_len), pid_str, pid_len);

    char cpu_s[8];
    int_to_str(p->cpu_percent, cpu_s);
    int mp = m.mem_total_kb > 0 ? (p->mem_kb * 100) / m.mem_total_kb : 0;
    char mem_s[8];
    int_to_str(mp, mem_s);
    char v_s[16];
    int_to_str(p->mem_kb, v_s);
    strcat(v_s, "K");

    if (content_w > 0) {
      int next = show_user ? x_user : (show_pri ? x_pri : x_state);
      draw_string_clipped(x_pid, py, pid_pad, 0xFFFFFF, 0x1a1b26, 1,
                          (next - x_pid) - 4);

      if (show_user) {
        next = show_pri ? x_pri : x_state;
        draw_string_clipped(x_user, py, p->user, 0x7dcfff, 0x1a1b26, 1,
                            (next - x_user) - 4);
      }

      if (show_pri) {
        draw_string_clipped(x_pri, py, "20", 0xAAAAAA, 0x1a1b26, 1,
                            (x_state - x_pri) - 4);
      }

      next = show_cpu ? x_cpu : (show_mem ? x_mem : (show_virt ? x_virt : x_cmd));
      draw_string_clipped(x_state, py, p->state,
                          (p->state[0] == 'R') ? 0x9ece6a : 0xAAAAAA,
                          0x1a1b26, 1, (next - x_state) - 4);

      if (show_cpu) {
        next = show_mem ? x_mem : (show_virt ? x_virt : x_cmd);
        draw_string_clipped(x_cpu, py, cpu_s, 0xFFFFFF, 0x1a1b26, 1,
                            (next - x_cpu) - 4);
      }

      if (show_mem) {
        next = show_virt ? x_virt : x_cmd;
        draw_string_clipped(x_mem, py, mem_s, 0xFFFFFF, 0x1a1b26, 1,
                            (next - x_mem) - 4);
      }

      if (show_virt) {
        draw_string_clipped(x_virt, py, v_s, 0xAAAAAA, 0x1a1b26, 1,
                            (x_cmd - x_virt) - 4);
      }

      draw_string_clipped(x_cmd, py, p->name, 0xFFFFFF, 0x1a1b26, 1,
                          content_right - x_cmd);
    }
    py += 14;
  }

  if (!selected_found)
    sysmon_selected_pid = -1;

  /* Enable end-task only for user processes (PID >= 100 in this demo). */
  sysmon_end_btn_enabled = (sysmon_selected_pid >= 100);
  uint32_t ebg = sysmon_end_btn_enabled ? 0x3d3d5c : 0x24283b;
  uint32_t efg = sysmon_end_btn_enabled ? 0xf7768e : 0x565f89;
  fb_fill_rect(sysmon_end_btn_x, sysmon_end_btn_y, sysmon_end_btn_w,
               sysmon_end_btn_h, ebg);
  fb_draw_rect(sysmon_end_btn_x, sysmon_end_btn_y, sysmon_end_btn_w,
               sysmon_end_btn_h, 0x565f89);
  fb_draw_string(sysmon_end_btn_x + 10, sysmon_end_btn_y + 5, "End Task", efg,
                 ebg, 1);

sysmon_done:
  /* Resize grip */
  if (!sysmon_maximized) {
    fb_fill_rect(sx + sw - RESIZE_GRIP, sy + sh - RESIZE_GRIP, RESIZE_GRIP,
                 RESIZE_GRIP, 0x24283b);
    fb_draw_char(sx + sw - 10, sy + sh - 10, '\\', 0x565f89, 0x24283b, 1);
  }
}

static void clamp_window_to_desktop(int *x, int *y, int w, int h) {
  if (!x || !y)
    return;
  int max_x = (int)fb_width() - w;
  int max_y = (int)fb_height() - TASKBAR_H - h;
  if (max_x < 0)
    max_x = 0;
  if (max_y < 0)
    max_y = 0;
  if (*x < 0)
    *x = 0;
  if (*y < 0)
    *y = 0;
  if (*x > max_x)
    *x = max_x;
  if (*y > max_y)
    *y = max_y;
}

static int point_in_rect(int px, int py, int x, int y, int w, int h) {
  return (px >= x && px < x + w && py >= y && py < y + h);
}

static int top_window_at(int mx, int my) {
  /* Matches draw_window_stack(): base order + active window last. */
  int base[] = {1, 5, 2, 3, 6, 7, 8, 9, 10, 4};
  int order[11];
  int n = 0;
  int active = active_window_focus;

  for (int i = 0; i < 10; i++) {
    if (base[i] == active)
      continue;
    order[n++] = base[i];
  }
  order[n++] = active;

  for (int i = n - 1; i >= 0; i--) {
    int id = order[i];
    if (id == 1) {
      if (win_vis && !win_minimized && point_in_rect(mx, my, wx, wy, ww, wh))
        return 1;
    } else if (id == 2) {
      if (about_vis && !about_minimized &&
          point_in_rect(mx, my, ax, ay, aw, ah))
        return 2;
    } else if (id == 3) {
      if (sysmon_vis && !sysmon_minimized &&
          point_in_rect(mx, my, sx, sy, sw, sh))
        return 3;
    } else if (id == 6) {
      if (bench_vis && !bench_minimized && point_in_rect(mx, my, bx, by, bw, bh))
        return 6;
    } else if (id == 7) {
      if (files_vis && !files_minimized && point_in_rect(mx, my, fx, fy, fw, fh))
        return 7;
    } else if (id == 8) {
      if (calc_vis && !calc_minimized &&
          point_in_rect(mx, my, calcx, calcy, calcw, calch))
        return 8;
    } else if (id == 9) {
      if (note_vis && !note_minimized &&
          point_in_rect(mx, my, notex, notey, notew, noteh))
        return 9;
    } else if (id == 10) {
      if (browser_vis && !browser_minimized &&
          point_in_rect(mx, my, brx, bry, brw, brh))
        return 10;
    } else if (id == 4) {
      if (settings_vis && !settings_minimized &&
          point_in_rect(mx, my, settx, setty, settw, setth))
        return 4;
    } else if (id == 5) {
      if (!doom_running || !doom_vis || doom_minimized)
        continue;
      int dw = doom_maximized ? (int)fb_width() : doom_w;
      int dh_total =
          doom_maximized ? ((int)fb_height() - TASKBAR_H) : (doom_h + TITLE_H);
      int bx = doom_maximized ? 0 : doom_x;
      int by = doom_maximized ? 0 : doom_y;
      if (point_in_rect(mx, my, bx, by, dw, dh_total))
        return 5;
    }
  }

  return 0;
}

/* ============================================================
 * Settings Window
 * ============================================================ */
static const char *settings_tab_names[] = {"Display", "System", "Network",
                                           "About"};
static const uint32_t settings_tab_icons[] = {0x7dcfff, 0x9ece6a, 0xe0af68,
                                              0xbb9af7};

static int settings_sidebar_w_rt = SETTINGS_SIDEBAR_W;
static int settings_content_x_rt = 0, settings_content_y_rt = 0;
static int settings_content_w_rt = 0, settings_content_h_rt = 0;
static int settings_disp_value_w_rt = 160;
static int settings_display_ui_ok_rt = 0;

static int selector_value_w(int cw) {
  /* Keep selectors usable in narrow windows. */
  int w = cw - (DISP_CTRL_X + DISP_ARROW_W * 2 + 26);
  if (w > 160)
    w = 160;
  if (w < 80)
    w = 80;
  return w;
}

/* Draw a selector control:  label   [< value >] */
static void draw_selector(int cx, int cy, const char *label, const char *value,
                          int idx, int count, int cw) {
  int bx = cx + DISP_CTRL_X;
  int label_max = (bx - (cx + 20)) - 6;
  if (label_max < 0)
    label_max = 0;
  draw_string_clipped(cx + 20, cy, label, 0xAAAAAA, 0x1a1b26, 1, label_max);
  int val_w = selector_value_w(cw);
  settings_disp_value_w_rt = val_w;
  /* Left arrow */
  uint32_t la_bg = (idx > 0) ? 0x3d3d5c : 0x24283b;
  uint32_t la_fg = (idx > 0) ? 0xFFFFFF : 0x565f89;
  fb_fill_rect(bx, cy - 2, DISP_ARROW_W, DISP_ROW_H, la_bg);
  fb_draw_char(bx + 3, cy + 1, '<', la_fg, la_bg, 1);
  /* Value */
  fb_fill_rect(bx + DISP_ARROW_W, cy - 2, val_w, DISP_ROW_H, 0x24283b);
  draw_string_ellipsis(bx + DISP_ARROW_W + 8, cy, value, 0xFFFFFF, 0x24283b, 1,
                       val_w - 16);
  /* Right arrow */
  int rx = bx + DISP_ARROW_W + val_w;
  uint32_t ra_bg = (idx < count - 1) ? 0x3d3d5c : 0x24283b;
  uint32_t ra_fg = (idx < count - 1) ? 0xFFFFFF : 0x565f89;
  fb_fill_rect(rx, cy - 2, DISP_ARROW_W, DISP_ROW_H, ra_bg);
  fb_draw_char(rx + 3, cy + 1, '>', ra_fg, ra_bg, 1);
}

static void draw_settings_tab_display(int cx, int cy, int cw, int ch) {
  /* This tab has the densest layout; avoid drawing outside the window. */
  if (cw < 240 || ch < 290) {
    fb_draw_string(cx + 10, cy + 10, "Window too small", 0xf7768e, 0x1a1b26, 1);
    fb_draw_string(cx + 10, cy + 26, "Resize Settings to view options",
                   0xAAAAAA, 0x1a1b26, 1);
    return;
  }

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
                DISP_RES_COUNT, cw);
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
  draw_selector(cx, cy + DISP_ROW_SCALE, "UI Scale:",
                disp_scale_names[disp_scale_idx], disp_scale_idx,
                DISP_SCALE_COUNT, cw);
  fb_draw_string(cx + 20, cy + 114, "Font Size:     8x8 bitmap", 0x565f89,
                 0x1a1b26, 1);

  /* Section: Refresh Rate */
  fb_draw_string(cx + 10, cy + 134, "Refresh Rate", 0x7dcfff, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 146, cw - 20, 0x30363d);
  char hz_str[16];
  int_to_str(disp_hz_vals[disp_hz_idx], hz_str);
  strcat(hz_str, " Hz");
  draw_selector(cx, cy + DISP_ROW_HZ, "Rate:", hz_str, disp_hz_idx,
                DISP_HZ_COUNT, cw);
  fb_draw_string(cx + 20, cy + 176, "V-Sync:        Enabled", 0x565f89,
                 0x1a1b26, 1);

  /* Section: Color Depth */
  fb_draw_string(cx + 10, cy + 196, "Color Depth", 0x7dcfff, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 208, cw - 20, 0x30363d);
  draw_selector(cx, cy + DISP_ROW_DEPTH, "Bit Depth:",
                disp_depth_names[disp_depth_idx], disp_depth_idx,
                DISP_DEPTH_COUNT, cw);

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
  if (cw < 240 || ch < 260) {
    fb_draw_string(cx + 10, cy + 10, "Window too small", 0xf7768e, 0x1a1b26, 1);
    fb_draw_string(cx + 10, cy + 26, "Resize Settings to view options",
                   0xAAAAAA, 0x1a1b26, 1);
    return;
  }

  /* CPU Info */
  fb_draw_string(cx + 10, cy + 10, "Processor", 0x9ece6a, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 22, cw - 20, 0x30363d);

  char cpu_str[64];
  memset(cpu_str, 0, sizeof(cpu_str));
  get_cpu_string(cpu_str);
  draw_string_ellipsis(cx + 20, cy + 32, cpu_str, 0xFFFFFF, 0x1a1b26, 1,
                       cw - 40);
  draw_string_ellipsis(cx + 20, cy + 46, "Architecture:  x86_64", 0xAAAAAA,
                       0x1a1b26, 1, cw - 40);

  /* Memory */
  fb_draw_string(cx + 10, cy + 72, "Memory", 0x9ece6a, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 84, cw - 20, 0x30363d);

  char mem_str[32];
  int_to_str(sys_total_memory_mb, mem_str);
  strcat(mem_str, " MB Total RAM");
  draw_string_ellipsis(cx + 20, cy + 94, mem_str, 0xFFFFFF, 0x1a1b26, 1,
                       cw - 40);

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
  draw_string_ellipsis(cx + 20, cy + 130, pct_str, 0xAAAAAA, 0x1a1b26, 1,
                       cw - 40);

  /* Uptime */
  fb_draw_string(cx + 10, cy + 156, "Uptime", 0x9ece6a, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 168, cw - 20, 0x30363d);
  char up[32];
  timer_format_uptime(up, 32);
  draw_string_ellipsis(cx + 20, cy + 178, up, 0xFFFFFF, 0x1a1b26, 1, cw - 40);

  /* Kernel */
  fb_draw_string(cx + 10, cy + 204, "Kernel", 0x9ece6a, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 216, cw - 20, 0x30363d);
  draw_string_ellipsis(cx + 20, cy + 226, "akaOS Kernel 1.1", 0xFFFFFF,
                       0x1a1b26, 1, cw - 40);
  draw_string_ellipsis(cx + 20, cy + 240, "Boot: Limine v10.x (x86_64)",
                       0xAAAAAA, 0x1a1b26, 1, cw - 40);
}

static void draw_settings_tab_network(int cx, int cy, int cw, int ch) {
  if (cw < 240 || ch < 210) {
    fb_draw_string(cx + 10, cy + 10, "Window too small", 0xf7768e, 0x1a1b26, 1);
    fb_draw_string(cx + 10, cy + 26, "Resize Settings to view options",
                   0xAAAAAA, 0x1a1b26, 1);
    return;
  }

  fb_draw_string(cx + 10, cy + 10, "Network Status", 0xe0af68, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 22, cw - 20, 0x30363d);

  int ok = net_is_available();

  /* Status indicator */
  fb_fill_rect(cx + 20, cy + 36, 10, 10, ok ? 0x9ece6a : 0xf7768e);
  fb_draw_string(cx + 36, cy + 36, ok ? "Connected" : "Disconnected", 0xFFFFFF,
                 0x1a1b26, 1);

  draw_string_ellipsis(cx + 20, cy + 56, "Adapter:       e1000 (Intel)",
                       0xAAAAAA, 0x1a1b26, 1, cw - 40);
  draw_string_ellipsis(cx + 20, cy + 70, "Type:          Ethernet", 0xAAAAAA,
                       0x1a1b26, 1, cw - 40);
  draw_string_ellipsis(cx + 20, cy + 84, "IP Address:    10.0.2.15", 0xFFFFFF,
                       0x1a1b26, 1, cw - 40);
  draw_string_ellipsis(cx + 20, cy + 98, "Subnet Mask:   255.255.255.0",
                       0xAAAAAA, 0x1a1b26, 1, cw - 40);
  draw_string_ellipsis(cx + 20, cy + 112, "Gateway:       10.0.2.2",
                       0xAAAAAA, 0x1a1b26, 1, cw - 40);
  draw_string_ellipsis(cx + 20, cy + 126, "DNS:           10.0.2.3", 0xAAAAAA,
                       0x1a1b26, 1, cw - 40);

  fb_draw_string(cx + 10, cy + 156, "Statistics", 0xe0af68, 0x1a1b26, 1);
  fb_draw_hline(cx + 10, cy + 168, cw - 20, 0x30363d);
  uint32_t txp = 0, rxp = 0;
  net_get_packet_counts(&txp, &rxp);
  uint32_t st = 0, rctl = 0, rdh = 0, rdt = 0;
  net_get_e1000_debug(&st, &rctl, &rdh, &rdt);
  char t[16];
  char line[48];

  int_to_str((int)txp, t);
  strcpy(line, "Packets TX:    ");
  strcat(line, t);
  draw_string_ellipsis(cx + 20, cy + 178, line, 0xAAAAAA, 0x1a1b26, 1, cw - 40);

  int_to_str((int)rxp, t);
  strcpy(line, "Packets RX:    ");
  strcat(line, t);
  draw_string_ellipsis(cx + 20, cy + 192, line, 0xAAAAAA, 0x1a1b26, 1, cw - 40);

  /* Debug (small and unobtrusive) */
  if (ch >= 240) {
    char a[16], b[16];
    int_to_str((int)rdh, a);
    int_to_str((int)rdt, b);
    strcpy(line, "RX ring: RDH=");
    strcat(line, a);
    strcat(line, " RDT=");
    strcat(line, b);
    draw_string_ellipsis(cx + 20, cy + 206, line, 0x666666, 0x1a1b26, 1, cw - 40);
  }
}

static void draw_settings_tab_about(int cx, int cy, int cw, int ch) {
  if (cw < 240 || ch < 260) {
    fb_draw_string(cx + 10, cy + 10, "Window too small", 0xf7768e, 0x1a1b26, 1);
    fb_draw_string(cx + 10, cy + 26, "Resize Settings to view options",
                   0xAAAAAA, 0x1a1b26, 1);
    return;
  }

  /* OS Name big */
  fb_draw_string(cx + (cw - 5 * 16) / 2, cy + 20, "akaOS", 0x7dcfff, 0x1a1b26,
                 2);
  fb_draw_string(cx + (cw - 20 * 8) / 2, cy + 50, "A Modern x86_64 Kernel",
                 0x565f89, 0x1a1b26, 1);

  fb_draw_hline(cx + 20, cy + 70, cw - 40, 0x30363d);

  fb_draw_string(cx + 20, cy + 86, "Version:       1.1", 0xAAAAAA, 0x1a1b26,
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
  draw_string_ellipsis(cx + 20, cy + 198, "github.com/akaoperatingsystem/akaOS",
                       0x7aa2f7, 0x1a1b26, 1, cw - 40);

  /* License badge */
  fb_fill_rect(cx + 20, cy + 224, 80, 18, 0x3d3d5c);
  fb_draw_string(cx + 26, cy + 229, "MIT License", 0x9ece6a, 0x3d3d5c, 1);
}

static void draw_settings_window(void) {
  if (!settings_vis || settings_minimized)
    return;

  /* Shadow */
  fb_fill_rect(settx + 4, setty + 4, settw, setth, 0x050505);

  /* Background */
  fb_fill_rect(settx, setty, settw, setth, 0x1a1b26);
  fb_draw_rect(settx, setty, settw, setth, 0x3d3d5c);

  /* Title bar */
  fb_fill_rect(settx, setty, settw, TITLE_H, 0x2a2a4a);
  /* Clip the title so it won't collide with window buttons in narrow widths. */
  int bclose_tmp = settx + settw - (BTN_PAD + BTN_SZ);
  int bmin_tmp = bclose_tmp - (BTN_SZ + 4) - (BTN_SZ + 4);
  int title_max = (bmin_tmp - (settx + 8)) - 6;
  draw_string_clipped(settx + 8, setty + 7, "Settings", 0xFFFFFF, 0x2a2a4a, 1,
                      title_max);

  /* Window buttons: [ - ] [ + ] [ x ] */
  int bclose = settx + settw - (BTN_PAD + BTN_SZ);
  int bmax = bclose - (BTN_SZ + 4);
  int bmin = bmax - (BTN_SZ + 4);
  fb_fill_rect(bclose, setty + BTN_PAD, BTN_SZ, BTN_SZ, 0xf7768e);
  fb_draw_char(bclose + 2, setty + 7, 'x', 0xFFFFFF, 0xf7768e, 1);
  fb_fill_rect(bmax, setty + BTN_PAD, BTN_SZ, BTN_SZ, 0x9ece6a);
  fb_draw_char(bmax + 2, setty + 7, '+', 0xFFFFFF, 0x9ece6a, 1);
  fb_fill_rect(bmin, setty + BTN_PAD, BTN_SZ, BTN_SZ, 0xe0af68);
  fb_draw_char(bmin + 2, setty + 7, '-', 0xFFFFFF, 0xe0af68, 1);

  /* Sidebar */
  int sidebar_x = settx;
  int sidebar_y = setty + TITLE_H;
  int sidebar_h = setth - TITLE_H;
  int sidebar_w = SETTINGS_SIDEBAR_W;
  int min_content_w = 240;
  if (settw - sidebar_w - 1 < min_content_w)
    sidebar_w = settw - min_content_w - 1;
  if (sidebar_w < 44)
    sidebar_w = 44;
  settings_sidebar_w_rt = sidebar_w;

  fb_fill_rect(sidebar_x, sidebar_y, sidebar_w, sidebar_h, 0x16161e);
  fb_draw_hline(sidebar_x + sidebar_w, sidebar_y, 1,
                0x30363d); /* divider */
  for (int j = sidebar_y; j < sidebar_y + sidebar_h; j++)
    fb_put_pixel(sidebar_x + sidebar_w, j, 0x30363d);

  /* Tab buttons */
  int show_tab_text = (sidebar_w >= 90);
  for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
    int ty = sidebar_y + 8 + i * 28;
    uint32_t bg = (i == settings_tab) ? 0x2a2a4a : 0x16161e;
    uint32_t fg = (i == settings_tab) ? settings_tab_icons[i] : 0x565f89;
    fb_fill_rect(sidebar_x + 4, ty, sidebar_w - 8, 24, bg);
    if (i == settings_tab) {
      /* Active indicator bar */
      fb_fill_rect(sidebar_x + 2, ty, 3, 24, settings_tab_icons[i]);
    }
    fb_fill_rect(sidebar_x + 10, ty + 8, 8, 8, fg);
    if (show_tab_text) {
      draw_string_clipped(sidebar_x + 24, ty + 8, settings_tab_names[i], fg, bg,
                          1, sidebar_w - 30);
    }
  }

  /* Content area */
  int cx = settx + sidebar_w + 1;
  int cy = setty + TITLE_H;
  int cw = settw - sidebar_w - 1;
  int ch = setth - TITLE_H;
  settings_content_x_rt = cx;
  settings_content_y_rt = cy;
  settings_content_w_rt = cw;
  settings_content_h_rt = ch;
  settings_display_ui_ok_rt = (settings_tab == 0 && cw >= 240 && ch >= 290);

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

  /* Resize grip */
  if (!settings_maximized) {
    fb_fill_rect(settx + settw - RESIZE_GRIP, setty + setth - RESIZE_GRIP,
                 RESIZE_GRIP, RESIZE_GRIP, 0x24283b);
    fb_draw_char(settx + settw - 10, setty + setth - 10, '\\', 0x565f89,
                 0x24283b, 1);
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
  180 /* Base height: title + categories + Browser + Notepad + Calculator + Reboot + Shutdown */
#define SUBMENU_H 120      /* System: 6 items * 20px */
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
    fb_draw_string(mx + 28, iy + 80, "Benchmark", 0xAAAAAA, 0x1e1e2e, 1);
    fb_draw_string(mx + 28, iy + 100, "File Manager", 0xAAAAAA, 0x1e1e2e, 1);
    iy += 120;
  }

  /* "Games >" or "Games v" category */
  fb_draw_string(mx + 10, iy, games_submenu_open ? "v Games" : "> Games",
                 0x9ece6a, 0x1e1e2e, 1);
  iy += 20;

  if (games_submenu_open) {
    fb_draw_string(mx + 28, iy, "DOOM", 0xAAAAAA, 0x1e1e2e, 1);
    iy += 20;
  }

  /* Web Browser (top-level) */
  fb_draw_string(mx + 10, iy, "Web Browser", 0xAAAAAA, 0x1e1e2e, 1);
  iy += 20;

  /* Notepad (top-level) */
  fb_draw_string(mx + 10, iy, "Notepad", 0xAAAAAA, 0x1e1e2e, 1);
  iy += 20;

  /* Calculator (top-level) */
  fb_draw_string(mx + 10, iy, "Calculator", 0xAAAAAA, 0x1e1e2e, 1);
  iy += 20;

  /* Reboot */
  fb_draw_string(mx + 10, iy, "Reboot System", 0xf7768e, 0x1e1e2e, 1);
  iy += 20;
  fb_draw_string(mx + 10, iy, "Shutdown", 0xf7768e, 0x1e1e2e, 1);
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
  clamp_window_to_desktop(&wx, &wy, ww, wh);

  ax = ((int)fb_width() - aw) / 2;
  ay = ((int)fb_height() - TASKBAR_H - ah) / 2;
  clamp_window_to_desktop(&ax, &ay, aw, ah);

  sx = ((int)fb_width() - sw) / 2;
  sy = ((int)fb_height() - TASKBAR_H - sh) / 2;
  clamp_window_to_desktop(&sx, &sy, sw, sh);

  bx = ((int)fb_width() - bw) / 2;
  by = ((int)fb_height() - TASKBAR_H - bh) / 2;
  clamp_window_to_desktop(&bx, &by, bw, bh);

  fx = ((int)fb_width() - fw) / 2;
  fy = ((int)fb_height() - TASKBAR_H - fh) / 2;
  clamp_window_to_desktop(&fx, &fy, fw, fh);

  calcx = ((int)fb_width() - calcw) / 2;
  calcy = ((int)fb_height() - TASKBAR_H - calch) / 2;
  clamp_window_to_desktop(&calcx, &calcy, calcw, calch);

  notex = ((int)fb_width() - notew) / 2;
  notey = ((int)fb_height() - TASKBAR_H - noteh) / 2;
  clamp_window_to_desktop(&notex, &notey, notew, noteh);

  brx = ((int)fb_width() - brw) / 2;
  bry = ((int)fb_height() - TASKBAR_H - brh) / 2;
  clamp_window_to_desktop(&brx, &bry, brw, brh);

  clen = 0;
  tcx = 0;
  tcy = 0;
  start_menu_vis = 0;
  about_vis = 0;
  sysmon_vis = 0;
  bench_vis = 0;
  files_vis = 0;
  calc_vis = 0;
  note_vis = 0;
  browser_vis = 0;
  settings_vis = 0;
  settings_tab = 0;

  settx = ((int)fb_width() - settw) / 2;
  setty = ((int)fb_height() - TASKBAR_H - setth) / 2;
  clamp_window_to_desktop(&settx, &setty, settw, setth);

  win_minimized = 0;
  win_maximized = 0;
  about_minimized = 0;
  about_maximized = 0;
  sysmon_minimized = 0;
  sysmon_maximized = 0;
  settings_minimized = 0;
  settings_maximized = 0;
  calc_minimized = 0;
  calc_maximized = 0;
  note_minimized = 0;
  note_maximized = 0;
  browser_minimized = 0;
  browser_maximized = 0;

  gui_mode_active = 1;

  sysmon_init(); /* Initialize backend tracking! */

  gui_term_set_color(0x7dcfff);
  gui_term_print("  Welcome to akaOS Terminal\n");
  gui_term_set_color(0xa9b1d6);
  gui_term_print("  Type 'help' for commands.\n\n");
  shell_print_prompt();

  calc_reset_all();
  note_reset();
  br_init();
}

static void draw_window_by_focus(int focus_id) {
  switch (focus_id) {
  case 1:
    draw_window();
    break;
  case 2:
    draw_about_window();
    break;
  case 3:
    draw_sysmon_window();
    break;
  case 6:
    draw_bench_window();
    break;
  case 7:
    draw_files_window();
    break;
  case 8:
    draw_calc_window();
    break;
  case 9:
    draw_notepad_window();
    break;
  case 10:
    draw_browser_window();
    break;
  case 4:
    draw_settings_window();
    break;
  case 5:
    if (doom_running)
      draw_doom_window();
    break;
  default:
    break;
  }
}

static void draw_window_stack(void) {
  /* Base stacking order (bottom to top). Active window is drawn last. */
  int order[] = {1, 5, 2, 3, 6, 7, 8, 9, 10, 4};
  int active = active_window_focus;

  for (int i = 0; i < 10; i++) {
    if (order[i] == active)
      continue;
    draw_window_by_focus(order[i]);
  }
  draw_window_by_focus(active);
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

  bench_step();

  draw_desktop();
  draw_window_stack();
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
static int _pid_bench = -1;
static int _pid_files = -1;
static int _pid_calc = -1;
static int _pid_note = -1;
static int _pid_browser = -1;

static void end_task_by_pid(int pid) {
  if (pid < 0)
    return;

  /* Core kernel pseudo-processes should not be killable from the UI. */
  if (pid >= 0 && pid <= 4)
    return;

  if (pid == _pid_term) {
    win_vis = 0;
    win_minimized = 0;
    win_maximized = 0;
    if (active_window_focus == 1)
      active_window_focus = 0;
    os_process_kill(pid);
    _pid_term = -1;
    return;
  }

  if (pid == _pid_sysmon) {
    sysmon_vis = 0;
    sysmon_minimized = 0;
    sysmon_maximized = 0;
    if (active_window_focus == 3)
      active_window_focus = 0;
    os_process_kill(pid);
    _pid_sysmon = -1;
    return;
  }

  if (pid == _pid_settings) {
    settings_vis = 0;
    settings_minimized = 0;
    settings_maximized = 0;
    if (active_window_focus == 4)
      active_window_focus = 0;
    os_process_kill(pid);
    _pid_settings = -1;
    return;
  }

  if (pid == _pid_about) {
    about_vis = 0;
    about_minimized = 0;
    about_maximized = 0;
    if (active_window_focus == 2)
      active_window_focus = 0;
    os_process_kill(pid);
    _pid_about = -1;
    return;
  }

  if (pid == _pid_doom) {
    doom_vis = 0;
    doom_minimized = 0;
    doom_maximized = 0;
    doom_running = 0;
    if (active_window_focus == 5)
      active_window_focus = 0;

    extern void (*keyboard_event_hook)(int, int);
    keyboard_event_hook = 0;

    os_process_kill(pid);
    _pid_doom = -1;
    return;
  }

  if (pid == _pid_bench) {
    bench_vis = 0;
    bench_minimized = 0;
    bench_maximized = 0;
    bench_reset();
    if (active_window_focus == 6)
      active_window_focus = 0;
    os_process_kill(pid);
    _pid_bench = -1;
    return;
  }

  if (pid == _pid_files) {
    files_vis = 0;
    files_minimized = 0;
    files_maximized = 0;
    fm_dir = 0;
    fm_selected = -1;
    if (active_window_focus == 7)
      active_window_focus = 0;
    os_process_kill(pid);
    _pid_files = -1;
    return;
  }

      if (pid == _pid_calc) {
        calc_vis = 0;
        calc_minimized = 0;
        calc_maximized = 0;
        if (active_window_focus == 8)
          active_window_focus = 0;
        os_process_kill(pid);
        _pid_calc = -1;
        return;
      }

  if (pid == _pid_note) {
    note_vis = 0;
    note_minimized = 0;
    note_maximized = 0;
    if (active_window_focus == 9)
      active_window_focus = 0;
    os_process_kill(pid);
    _pid_note = -1;
    return;
  }

  if (pid == _pid_browser) {
    browser_vis = 0;
    browser_minimized = 0;
    browser_maximized = 0;
    if (active_window_focus == 10)
      active_window_focus = 0;
    os_process_kill(pid);
    _pid_browser = -1;
    return;
  }

  /* Fallback: remove it from the process table if it exists. */
  os_process_kill(pid);
}

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

  if (bench_vis && _pid_bench < 0)
    _pid_bench = os_process_spawn("bench", "root", 256);
  if (!bench_vis && _pid_bench >= 0) {
    os_process_kill(_pid_bench);
    _pid_bench = -1;
  }

  if (files_vis && _pid_files < 0)
    _pid_files = os_process_spawn("files", "root", 256);
  if (!files_vis && _pid_files >= 0) {
    os_process_kill(_pid_files);
    _pid_files = -1;
  }

  if (calc_vis && _pid_calc < 0)
    _pid_calc = os_process_spawn("calc", "root", 128);
  if (!calc_vis && _pid_calc >= 0) {
    os_process_kill(_pid_calc);
    _pid_calc = -1;
  }

  if (note_vis && _pid_note < 0)
    _pid_note = os_process_spawn("notepad", "root", 256);
  if (!note_vis && _pid_note >= 0) {
    os_process_kill(_pid_note);
    _pid_note = -1;
  }

  if (browser_vis && _pid_browser < 0)
    _pid_browser = os_process_spawn("browser", "root", 256);
  if (!browser_vis && _pid_browser >= 0) {
    os_process_kill(_pid_browser);
    _pid_browser = -1;
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
            win_minimized = 0;
            active_window_focus = 1;
            start_menu_vis = 0;
            sys_submenu_open = 0;
          } else if (sys_submenu_open && item_y >= sub_start + 20 &&
                     item_y < sub_start + 40) {
            /* System Monitor */
            sysmon_vis = 1;
            sysmon_minimized = 0;
            sysmon_core_offset = 0;
            sysmon_tab = 0;
            active_window_focus = 3;
            start_menu_vis = 0;
            sys_submenu_open = 0;
          } else if (sys_submenu_open && item_y >= sub_start + 40 &&
                     item_y < sub_start + 60) {
            /* About akaOS */
            about_vis = 1;
            about_minimized = 0;
            active_window_focus = 2;
            start_menu_vis = 0;
            sys_submenu_open = 0;
          } else if (sys_submenu_open && item_y >= sub_start + 60 &&
                     item_y < sub_start + 80) {
            /* Settings */
            settings_vis = 1;
            settings_minimized = 0;
            active_window_focus = 4;
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
          } else if (sys_submenu_open && item_y >= sub_start + 80 &&
                     item_y < sub_start + 100) {
            /* Benchmark */
            bench_vis = 1;
            bench_minimized = 0;
            active_window_focus = 6;
            start_menu_vis = 0;
            sys_submenu_open = 0;
          } else if (sys_submenu_open && item_y >= sub_start + 100 &&
                     item_y < sub_start + 120) {
            /* File Manager */
            files_vis = 1;
            files_minimized = 0;
            active_window_focus = 7;
            fm_dir = 0;
            fm_selected = -1;
            start_menu_vis = 0;
            sys_submenu_open = 0;
          } else {
            int games_row = sub_start + (sys_submenu_open ? 120 : 0);
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
              /* Last items after submenus: Browser, Notepad, Calculator, Reboot, Shutdown */
              int browser_y = games_sub_start + (games_submenu_open ? 20 : 0);
              int note_y = browser_y + 20;
              int calc_y = note_y + 20;
              int reboot_y = calc_y + 20;
              int shutdown_y = reboot_y + 20;
              if (item_y >= browser_y && item_y < browser_y + 20) {
                browser_vis = 1;
                browser_minimized = 0;
                active_window_focus = 10;
                start_menu_vis = 0;
                sys_submenu_open = 0;
                games_submenu_open = 0;
                br_init();
              } else if (item_y >= note_y && item_y < note_y + 20) {
                note_vis = 1;
                note_minimized = 0;
                active_window_focus = 9;
                start_menu_vis = 0;
                sys_submenu_open = 0;
                games_submenu_open = 0;
                note_reset();
              } else if (item_y >= calc_y && item_y < calc_y + 20) {
                calc_vis = 1;
                calc_minimized = 0;
                active_window_focus = 8;
                start_menu_vis = 0;
                sys_submenu_open = 0;
                games_submenu_open = 0;
                calc_reset_all();
              } else if (item_y >= reboot_y && item_y < reboot_y + 20) {
                uint8_t g = 0x02;
                while (g & 0x02)
                  g = inb(0x64);
                outb(0x64, 0xFE);
                asm volatile("cli; hlt");
              } else if (item_y >= shutdown_y && item_y < shutdown_y + 20) {
                /* Power off (QEMU/Bochs) */
                outw(0x604, 0x2000);
                outw(0xB004, 0x2000);
                outw(0x4004, 0x3400);
                asm volatile("cli");
                for (;;)
                  asm volatile("hlt");
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
        int top_win = top_window_at(mx, my);

        /* Settings Window (top most) */
        if (settings_vis && !clicked_win && top_win == 4) {
          /* Resize grip gets priority over body clicks */
          if (!settings_minimized && !settings_maximized &&
              mx >= settx + settw - RESIZE_GRIP && mx < settx + settw &&
              my >= setty + setth - RESIZE_GRIP && my < setty + setth) {
            dragging = 104; /* settings resize */
            resize_base_w = settw;
            resize_base_h = setth;
            resize_start_x = mx;
            resize_start_y = my;
            clicked_win = 1;
            active_window_focus = 4;
          } else if (mx >= settx && mx < settx + settw && my >= setty &&
                     my < setty + TITLE_H) {
            int x_close = settx + settw - (BTN_PAD + BTN_SZ);
            int x_max = x_close - (BTN_SZ + 4);
            int x_min = x_max - (BTN_SZ + 4);
            if (mx >= x_close && mx < x_close + BTN_SZ &&
                my >= setty + BTN_PAD && my < setty + BTN_PAD + BTN_SZ) {
              settings_vis = 0;
            } else if (mx >= x_max && mx < x_max + BTN_SZ &&
                       my >= setty + BTN_PAD && my < setty + BTN_PAD + BTN_SZ) {
              if (!settings_maximized) {
                settings_prev_x = settx;
                settings_prev_y = setty;
                settings_prev_w = settw;
                settings_prev_h = setth;
                settings_maximized = 1;
                settx = 0;
                setty = 0;
                settw = (int)fb_width();
                setth = (int)fb_height() - TASKBAR_H;
              } else {
                settings_maximized = 0;
                settx = settings_prev_x;
                setty = settings_prev_y;
                settw = settings_prev_w;
                setth = settings_prev_h;
              }
            } else if (mx >= x_min && mx < x_min + BTN_SZ &&
                       my >= setty + BTN_PAD && my < setty + BTN_PAD + BTN_SZ) {
              settings_minimized = 1;
            } else if (!settings_maximized) {
              dragging = 4;
              dox = mx - settx;
              doy = my - setty;
            }
            clicked_win = 1;
          } else if (mx >= settx && mx < settx + settings_sidebar_w_rt &&
                     my >= setty + TITLE_H && my < setty + setth) {
            /* Tab clicks */
            int rel_y = my - (setty + TITLE_H) - 8;
            int tab_idx = rel_y / 28;
            if (tab_idx >= 0 && tab_idx < SETTINGS_TAB_COUNT) {
              settings_tab = tab_idx;
            }
            clicked_win = 1;
          } else if (mx >= settings_content_x_rt && mx < settx + settw &&
                     my >= setty + TITLE_H && my < setty + setth &&
                     settings_tab == 0) {
            /* Display tab content clicks — detect arrow buttons */
            if (!settings_display_ui_ok_rt) {
              clicked_win = 1;
              active_window_focus = 4;
            } else {
            int content_x = settings_content_x_rt;
            int content_y = settings_content_y_rt;
            int ctrl_abs_x = content_x + DISP_CTRL_X;
            int left_x1 = ctrl_abs_x;
            int left_x2 = ctrl_abs_x + DISP_ARROW_W;
            int right_x1 = ctrl_abs_x + DISP_ARROW_W + settings_disp_value_w_rt;
            int right_x2 = right_x1 + DISP_ARROW_W;
            /* Check each row */
            int rows_y[] = {DISP_ROW_RES, DISP_ROW_SCALE, DISP_ROW_HZ,
                            DISP_ROW_DEPTH};
            int *idxs[] = {&disp_res_idx, &disp_scale_idx, &disp_hz_idx,
                           &disp_depth_idx};
            int maxs[] = {DISP_RES_COUNT, DISP_SCALE_COUNT, DISP_HZ_COUNT,
                          DISP_DEPTH_COUNT};
            if (settings_content_w_rt >= 240) {
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
            }
            /* Apply button click */
            int cw_inner = settings_content_w_rt;
            int ch_inner = settings_content_h_rt;
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
              bx = (nw - bw) / 2;
              by = (nh - TASKBAR_H - bh) / 2;
              fx = (nw - fw) / 2;
              fy = (nh - TASKBAR_H - fh) / 2;
              calcx = (nw - calcw) / 2;
              calcy = (nh - TASKBAR_H - calch) / 2;
              notex = (nw - notew) / 2;
              notey = (nh - TASKBAR_H - noteh) / 2;
              brx = (nw - brw) / 2;
              bry = (nh - TASKBAR_H - brh) / 2;
              settx = (nw - settw) / 2;
              setty = (nh - TASKBAR_H - setth) / 2;
              clamp_window_to_desktop(&wx, &wy, ww, wh);
              clamp_window_to_desktop(&ax, &ay, aw, ah);
              clamp_window_to_desktop(&sx, &sy, sw, sh);
              clamp_window_to_desktop(&bx, &by, bw, bh);
              clamp_window_to_desktop(&fx, &fy, fw, fh);
              clamp_window_to_desktop(&calcx, &calcy, calcw, calch);
              clamp_window_to_desktop(&notex, &notey, notew, noteh);
              clamp_window_to_desktop(&brx, &bry, brw, brh);
              clamp_window_to_desktop(&settx, &setty, settw, setth);
            }
            clicked_win = 1;
            active_window_focus = 4;
            }
          } else if (mx >= settx && mx < settx + settw && my >= setty &&
                     my < setty + setth) {
            clicked_win = 1;
            active_window_focus = 4;
          }
        }

        /* Benchmark Window */
        if (bench_vis && !clicked_win && top_win == 6) {
          /* Resize grip gets priority over body clicks */
          if (!bench_minimized && !bench_maximized &&
              mx >= bx + bw - RESIZE_GRIP && mx < bx + bw &&
              my >= by + bh - RESIZE_GRIP && my < by + bh) {
            dragging = 106; /* bench resize */
            resize_base_w = bw;
            resize_base_h = bh;
            resize_start_x = mx;
            resize_start_y = my;
            clicked_win = 1;
            active_window_focus = 6;
          } else if (mx >= bx && mx < bx + bw && my >= by && my < by + TITLE_H) {
            int x_close = bx + bw - (BTN_PAD + BTN_SZ);
            int x_max = x_close - (BTN_SZ + 4);
            int x_min = x_max - (BTN_SZ + 4);
            if (mx >= x_close && mx < x_close + BTN_SZ &&
                my >= by + BTN_PAD && my < by + BTN_PAD + BTN_SZ) {
              bench_vis = 0;
              bench_reset();
            } else if (mx >= x_max && mx < x_max + BTN_SZ &&
                       my >= by + BTN_PAD && my < by + BTN_PAD + BTN_SZ) {
              if (!bench_maximized) {
                bench_prev_x = bx;
                bench_prev_y = by;
                bench_prev_w = bw;
                bench_prev_h = bh;
                bench_maximized = 1;
                bx = 0;
                by = 0;
                bw = (int)fb_width();
                bh = (int)fb_height() - TASKBAR_H;
              } else {
                bench_maximized = 0;
                bx = bench_prev_x;
                by = bench_prev_y;
                bw = bench_prev_w;
                bh = bench_prev_h;
              }
            } else if (mx >= x_min && mx < x_min + BTN_SZ &&
                       my >= by + BTN_PAD && my < by + BTN_PAD + BTN_SZ) {
              bench_minimized = 1;
            } else if (!bench_maximized) {
              dragging = 6;
              dox = mx - bx;
              doy = my - by;
            }
            clicked_win = 1;
            active_window_focus = 6;
          } else if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
            /* Mode buttons + start/stop */
            int cx = bx + 14;
            int opt_y = by + TITLE_H + 14 + 18;
            int opt_w = 90;
            for (int i = 0; i < 3; i++) {
              int rx = cx + i * (opt_w + 8);
              if (mx >= rx && mx < rx + opt_w && my >= opt_y &&
                  my < opt_y + 18 && !bench_running) {
                bench_mode = i;
              }
            }

            int btn_y = opt_y + 34;
            if (mx >= cx && mx < cx + 110 && my >= btn_y && my < btn_y + 20) {
              if (!bench_running)
                bench_start();
            }
            if (mx >= cx + 120 && mx < cx + 230 && my >= btn_y &&
                my < btn_y + 20) {
              if (bench_running) {
                bench_running = 0;
                bench_phase = 0;
                bench_progress = 0;
              }
            }

            clicked_win = 1;
            active_window_focus = 6;
          }
        }

        /* File Manager Window */
        if (files_vis && !clicked_win && top_win == 7) {
          /* Resize grip gets priority over body clicks */
          if (!files_minimized && !files_maximized &&
              mx >= fx + fw - RESIZE_GRIP && mx < fx + fw &&
              my >= fy + fh - RESIZE_GRIP && my < fy + fh) {
            dragging = 107; /* files resize */
            resize_base_w = fw;
            resize_base_h = fh;
            resize_start_x = mx;
            resize_start_y = my;
            clicked_win = 1;
            active_window_focus = 7;
          } else if (mx >= fx && mx < fx + fw && my >= fy && my < fy + TITLE_H) {
            int x_close = fx + fw - (BTN_PAD + BTN_SZ);
            int x_max = x_close - (BTN_SZ + 4);
            int x_min = x_max - (BTN_SZ + 4);
            if (mx >= x_close && mx < x_close + BTN_SZ &&
                my >= fy + BTN_PAD && my < fy + BTN_PAD + BTN_SZ) {
              files_vis = 0;
              fm_dir = 0;
              fm_selected = -1;
              fm_prompt_close();
            } else if (mx >= x_max && mx < x_max + BTN_SZ &&
                       my >= fy + BTN_PAD && my < fy + BTN_PAD + BTN_SZ) {
              if (!files_maximized) {
                files_prev_x = fx;
                files_prev_y = fy;
                files_prev_w = fw;
                files_prev_h = fh;
                files_maximized = 1;
                fx = 0;
                fy = 0;
                fw = (int)fb_width();
                fh = (int)fb_height() - TASKBAR_H;
              } else {
                files_maximized = 0;
                fx = files_prev_x;
                fy = files_prev_y;
                fw = files_prev_w;
                fh = files_prev_h;
              }
            } else if (mx >= x_min && mx < x_min + BTN_SZ &&
                       my >= fy + BTN_PAD && my < fy + BTN_PAD + BTN_SZ) {
              files_minimized = 1;
            } else if (!files_maximized) {
              dragging = 7;
              dox = mx - fx;
              doy = my - fy;
            }
            clicked_win = 1;
            active_window_focus = 7;
          } else if (mx >= fx && mx < fx + fw && my >= fy && my < fy + fh) {
            /* Toolbar up button */
            int tx = fx + 10;
            int ty = fy + TITLE_H + 8;
            /* Modal prompt gets priority over all other clicks */
            if (fm_prompt_vis) {
              int mw = 320;
              int mh = 120;
              if (mw > fw - 40)
                mw = fw - 40;
              if (mw < 220)
                mw = 220;
              int mx0 = fx + (fw - mw) / 2;
              int my0 = fy + TITLE_H + 40;
              if (my0 + mh > fy + fh - 20)
                my0 = fy + fh - mh - 20;

              int by = my0 + mh - 28;
              int okx = mx0 + mw - 150;
              int cancel_x = mx0 + mw - 74;
              if (mx >= okx && mx < okx + 70 && my >= by && my < by + 20) {
                fm_create_folder_named(fm_prompt_buf);
              } else if (mx >= cancel_x && mx < cancel_x + 70 && my >= by &&
                         my < by + 20) {
                fm_prompt_close();
              }
            } else if (mx >= tx && mx < tx + 38 && my >= ty && my < ty + 18) {
              if (fm_dir && fm_dir != fs_get_root())
                fm_open_dir(fm_dir->parent);
            } else if (mx >= tx + 46 && mx < tx + 46 + 90 && my >= ty &&
                       my < ty + 18) {
              /* New Folder */
              fm_prompt_open();
            } else if (mx >= tx + 46 + 98 && mx < tx + 46 + 98 + 62 &&
                       my >= ty && my < ty + 18) {
              /* Delete (only when a valid selection exists) */
              if (fm_dir && fm_selected >= 0 && fm_selected < fm_dir->child_count)
                fm_delete_selected();
            } else if (mx >= tx + 46 + 98 + 70 && mx < tx + 46 + 98 + 70 + 74 &&
                       my >= ty && my < ty + 18) {
              /* Open in Terminal */
              fm_open_in_terminal();
            } else {
              /* List click */
              int cx = fx + 10;
              int cy = fy + TITLE_H + 36;
              int ch = fh - (TITLE_H + 46);
              int list_w = (fw * 55) / 100;
              if (list_w < 240)
                list_w = 240;
              if (list_w > fw - 180)
                list_w = fw - 180;

              int list_x = cx;
              int list_y = cy;
              if (mx >= list_x && mx < list_x + list_w &&
                  my >= list_y + 24 && my < list_y + ch) {
                int row_h = 14;
                int rel = my - (list_y + 24);
                int idx = rel / row_h;

                if (fm_dir && fm_dir != fs_get_root()) {
                  if (idx == 0) {
                    /* Parent */
                    uint64_t now = timer_get_ticks();
                    if (fm_last_click_idx == -2 && (now - fm_last_click_tick) < 20)
                      fm_open_dir(fm_dir->parent);
                    fm_selected = -2;
                    fm_last_click_idx = -2;
                    fm_last_click_tick = now;
                    clicked_win = 1;
                    active_window_focus = 7;
                    goto files_click_done;
                  }
                  idx -= 1;
                }

                if (fm_dir && idx >= 0 && idx < fm_dir->child_count) {
                  uint64_t now = timer_get_ticks();
                  if (fm_last_click_idx == idx && (now - fm_last_click_tick) < 20) {
                    /* Double click opens directories */
                    fs_node_t *n = fm_dir->children[idx];
                    if (n && n->type == FS_DIRECTORY)
                      fm_open_dir(n);
                  } else {
                    fm_selected = idx;
                  }
                  fm_last_click_idx = idx;
                  fm_last_click_tick = now;
                }
              }
            }
            clicked_win = 1;
            active_window_focus = 7;
files_click_done:
            (void)0;
          }
        }

        /* Calculator Window */
        if (calc_vis && !clicked_win && top_win == 8) {
          /* Resize grip gets priority over body clicks */
          if (!calc_minimized && !calc_maximized &&
              mx >= calcx + calcw - RESIZE_GRIP && mx < calcx + calcw &&
              my >= calcy + calch - RESIZE_GRIP && my < calcy + calch) {
            dragging = 108; /* calc resize */
            resize_base_w = calcw;
            resize_base_h = calch;
            resize_start_x = mx;
            resize_start_y = my;
            clicked_win = 1;
            active_window_focus = 8;
          } else if (mx >= calcx && mx < calcx + calcw && my >= calcy &&
                     my < calcy + TITLE_H) {
            int x_close = calcx + calcw - (BTN_PAD + BTN_SZ);
            int x_max = x_close - (BTN_SZ + 4);
            int x_min = x_max - (BTN_SZ + 4);
            if (mx >= x_close && mx < x_close + BTN_SZ &&
                my >= calcy + BTN_PAD && my < calcy + BTN_PAD + BTN_SZ) {
              calc_vis = 0;
            } else if (mx >= x_max && mx < x_max + BTN_SZ &&
                       my >= calcy + BTN_PAD && my < calcy + BTN_PAD + BTN_SZ) {
              if (!calc_maximized) {
                calc_prev_x = calcx;
                calc_prev_y = calcy;
                calc_prev_w = calcw;
                calc_prev_h = calch;
                calc_maximized = 1;
                calcx = 0;
                calcy = 0;
                calcw = (int)fb_width();
                calch = (int)fb_height() - TASKBAR_H;
              } else {
                calc_maximized = 0;
                calcx = calc_prev_x;
                calcy = calc_prev_y;
                calcw = calc_prev_w;
                calch = calc_prev_h;
              }
            } else if (mx >= x_min && mx < x_min + BTN_SZ &&
                       my >= calcy + BTN_PAD && my < calcy + BTN_PAD + BTN_SZ) {
              calc_minimized = 1;
            } else if (!calc_maximized) {
              dragging = 8;
              dox = mx - calcx;
              doy = my - calcy;
            }
            clicked_win = 1;
            active_window_focus = 8;
          } else if (mx >= calcx && mx < calcx + calcw && my >= calcy &&
                     my < calcy + calch) {
            /* Button grid hit-test */
            int pad = 10;
            int disp_h = 42;
            int dx = calcx + pad;
            int dy = calcy + TITLE_H + pad;
            int dw = calcw - pad * 2;
            int grid_x = dx;
            int grid_y = dy + disp_h + 10;
            int grid_w = dw;
            int grid_h = calch - (grid_y - calcy) - 12;
            if (grid_h < 140)
              grid_h = 140;
            int cols = 4;
            int rows = 5;
            int gap = 6;
            int btn_w = (grid_w - gap * (cols - 1)) / cols;
            int btn_h = (grid_h - gap * (rows - 1)) / rows;
            if (btn_h > 42)
              btn_h = 42;
            if (btn_w < 40)
              btn_w = 40;

            if (mx >= grid_x && mx < grid_x + cols * btn_w + (cols - 1) * gap &&
                my >= grid_y && my < grid_y + rows * btn_h + (rows - 1) * gap) {
              int relx = mx - grid_x;
              int rely = my - grid_y;
              int col = relx / (btn_w + gap);
              int row = rely / (btn_h + gap);
              int in_gap_x = relx % (btn_w + gap);
              int in_gap_y = rely % (btn_h + gap);
              if (in_gap_x < btn_w && in_gap_y < btn_h && col >= 0 && col < cols &&
                  row >= 0 && row < rows) {
                int idx = row * cols + col;
                switch (idx) {
                case 0: calc_input_digit('7'); break;
                case 1: calc_input_digit('8'); break;
                case 2: calc_input_digit('9'); break;
                case 3: calc_press_op('/'); break;
                case 4: calc_input_digit('4'); break;
                case 5: calc_input_digit('5'); break;
                case 6: calc_input_digit('6'); break;
                case 7: calc_press_op('*'); break;
                case 8: calc_input_digit('1'); break;
                case 9: calc_input_digit('2'); break;
                case 10: calc_input_digit('3'); break;
                case 11: calc_press_op('-'); break;
                case 12: calc_input_digit('0'); break;
                case 13: calc_toggle_sign(); break;
                case 14: calc_reset_all(); break;
                case 15: calc_press_op('+'); break;
                case 16: calc_backspace(); break;
                case 17: calc_clear_entry(); break;
                case 18: calc_equals(); break;
                default: break;
                }
              }
            }
            clicked_win = 1;
            active_window_focus = 8;
          }
        }

        /* Notepad Window */
        if (note_vis && !clicked_win && top_win == 9) {
          /* Resize grip gets priority over body clicks */
          if (!note_minimized && !note_maximized &&
              mx >= notex + notew - RESIZE_GRIP && mx < notex + notew &&
              my >= notey + noteh - RESIZE_GRIP && my < notey + noteh) {
            dragging = 109; /* note resize */
            resize_base_w = notew;
            resize_base_h = noteh;
            resize_start_x = mx;
            resize_start_y = my;
            clicked_win = 1;
            active_window_focus = 9;
          } else if (mx >= notex && mx < notex + notew && my >= notey &&
                     my < notey + TITLE_H) {
            int x_close = notex + notew - (BTN_PAD + BTN_SZ);
            int x_max = x_close - (BTN_SZ + 4);
            int x_min = x_max - (BTN_SZ + 4);
            if (mx >= x_close && mx < x_close + BTN_SZ &&
                my >= notey + BTN_PAD && my < notey + BTN_PAD + BTN_SZ) {
              note_vis = 0;
              note_prompt_close();
            } else if (mx >= x_max && mx < x_max + BTN_SZ &&
                       my >= notey + BTN_PAD && my < notey + BTN_PAD + BTN_SZ) {
              if (!note_maximized) {
                note_prev_x = notex;
                note_prev_y = notey;
                note_prev_w = notew;
                note_prev_h = noteh;
                note_maximized = 1;
                notex = 0;
                notey = 0;
                notew = (int)fb_width();
                noteh = (int)fb_height() - TASKBAR_H;
              } else {
                note_maximized = 0;
                notex = note_prev_x;
                notey = note_prev_y;
                notew = note_prev_w;
                noteh = note_prev_h;
              }
            } else if (mx >= x_min && mx < x_min + BTN_SZ &&
                       my >= notey + BTN_PAD && my < notey + BTN_PAD + BTN_SZ) {
              note_minimized = 1;
            } else if (!note_maximized) {
              dragging = 9;
              dox = mx - notex;
              doy = my - notey;
            }
            clicked_win = 1;
            active_window_focus = 9;
          } else if (mx >= notex && mx < notex + notew && my >= notey &&
                     my < notey + noteh) {
            /* Modal prompt gets priority over all other clicks */
            if (note_prompt_vis) {
              int mw = 360;
              int mh = 120;
              if (mw > notew - 40)
                mw = notew - 40;
              if (mw < 240)
                mw = 240;
              int mx0 = notex + (notew - mw) / 2;
              int my0 = notey + TITLE_H + 60;
              if (my0 + mh > notey + noteh - 20)
                my0 = notey + noteh - mh - 20;

              int by = my0 + mh - 28;
              int okx = mx0 + mw - 150;
              int cancel_x = mx0 + mw - 74;
              if (mx >= okx && mx < okx + 70 && my >= by && my < by + 20) {
                if (note_prompt_mode == 1) {
                  if (!note_open_path(note_prompt_buf))
                    note_prompt_set_msg("Open failed", 180);
                  else
                    note_prompt_close();
                } else if (note_prompt_mode == 2) {
                  if (!note_save_path(note_prompt_buf))
                    note_prompt_set_msg("Save failed", 180);
                  else
                    note_prompt_close();
                }
              } else if (mx >= cancel_x && mx < cancel_x + 70 && my >= by &&
                         my < by + 20) {
                note_prompt_close();
              }
            } else {
              int pad = 10;
              int tx = notex + pad;
              int ty = notey + TITLE_H + 8;
              const int btn_h = 18;
              const int gap = 6;
              int b0 = tx;                 /* New: 44 */
              int b1 = b0 + 44 + gap;      /* Open: 52 */
              int b2 = b1 + 52 + gap;      /* Save: 52 */
              int b3 = b2 + 52 + gap;      /* SaveAs: 72 */

              /* Toolbar buttons */
              if (my >= ty && my < ty + btn_h) {
                if (mx >= b0 && mx < b0 + 44) {
                  note_reset();
                  note_set_status("New", 80);
                } else if (mx >= b1 && mx < b1 + 52) {
                  note_prompt_open(1);
                } else if (mx >= b2 && mx < b2 + 52) {
                  if (note_path[0]) {
                    if (!note_save_path(note_path))
                      note_set_status("Save failed", 180);
                  } else {
                    note_prompt_open(2);
                  }
                } else if (mx >= b3 && mx < b3 + 72) {
                  note_prompt_open(2);
                }
              }

              /* Click inside editor places cursor */
              int bx0 = notex + pad;
              int by0 = notey + TITLE_H + pad + NOTE_TOOLBAR_H + 14;
              int bw0 = notew - pad * 2;
              int bh0 = noteh - TITLE_H - pad * 2 - (NOTE_TOOLBAR_H + 14);
              int cols = bw0 / CHAR_W;
              int rows = bh0 / CHAR_H;
              if (cols < 1)
                cols = 1;
              if (rows < 1)
                rows = 1;
              note_scroll_keep_cursor_visible(cols, rows);
              if (mx >= bx0 && mx < bx0 + bw0 && my >= by0 && my < by0 + bh0) {
                int relx = mx - bx0;
                int rely = my - by0;
                int tcol = relx / CHAR_W;
                int tline = note_scroll_line + (rely / CHAR_H);
                int idx = note_index_from_xy(tline, tcol, cols);
                if (idx < 0)
                  idx = 0;
                if (idx > note_len)
                  idx = note_len;
                note_cur = idx;
              }
            }
            clicked_win = 1;
            active_window_focus = 9;
          }
        }

        /* Web Browser Window */
        if (browser_vis && !clicked_win && top_win == 10) {
          /* Resize grip gets priority over body clicks */
          if (!browser_minimized && !browser_maximized &&
              mx >= brx + brw - RESIZE_GRIP && mx < brx + brw &&
              my >= bry + brh - RESIZE_GRIP && my < bry + brh) {
            dragging = 110; /* browser resize */
            resize_base_w = brw;
            resize_base_h = brh;
            resize_start_x = mx;
            resize_start_y = my;
            clicked_win = 1;
            active_window_focus = 10;
          } else if (mx >= brx && mx < brx + brw && my >= bry &&
                     my < bry + TITLE_H) {
            int x_close = brx + brw - (BTN_PAD + BTN_SZ);
            int x_max = x_close - (BTN_SZ + 4);
            int x_min = x_max - (BTN_SZ + 4);
            if (mx >= x_close && mx < x_close + BTN_SZ &&
                my >= bry + BTN_PAD && my < bry + BTN_PAD + BTN_SZ) {
              browser_vis = 0;
              br_editing = 0;
            } else if (mx >= x_max && mx < x_max + BTN_SZ &&
                       my >= bry + BTN_PAD && my < bry + BTN_PAD + BTN_SZ) {
              if (!browser_maximized) {
                browser_prev_x = brx;
                browser_prev_y = bry;
                browser_prev_w = brw;
                browser_prev_h = brh;
                browser_maximized = 1;
                brx = 0;
                bry = 0;
                brw = (int)fb_width();
                brh = (int)fb_height() - TASKBAR_H;
              } else {
                browser_maximized = 0;
                brx = browser_prev_x;
                bry = browser_prev_y;
                brw = browser_prev_w;
                brh = browser_prev_h;
              }
            } else if (mx >= x_min && mx < x_min + BTN_SZ &&
                       my >= bry + BTN_PAD && my < bry + BTN_PAD + BTN_SZ) {
              browser_minimized = 1;
            } else if (!browser_maximized) {
              dragging = 10;
              dox = mx - brx;
              doy = my - bry;
            }
            clicked_win = 1;
            active_window_focus = 10;
          } else if (mx >= brx && mx < brx + brw && my >= bry && my < bry + brh) {
            /* Toolbar + links */
            int pad = 10;
            int tx = brx + pad;
            int ty = bry + TITLE_H + 8;
            int addr_x = tx + 144;
            int addr_w = brw - pad * 2 - 144;
            if (addr_w < 60)
              addr_w = 60;
            if (my >= ty && my < ty + 18) {
              if (mx >= tx && mx < tx + 44) {
                br_back();
              } else if (mx >= tx + 50 && mx < tx + 50 + 44) {
                br_forward();
              } else if (mx >= tx + 100 && mx < tx + 100 + 36) {
                br_go_addr();
              } else if (mx >= addr_x && mx < addr_x + addr_w) {
                br_editing = 1;
              }
            }

            /* Links list click */
            int cx = brx + pad;
            int cy = bry + TITLE_H + 44;
            int cw = brw - pad * 2;
            int ch = brh - (cy - bry) - 12;
            int link_h = ch / 3;
            if (link_h > 120)
              link_h = 120;
            if (link_h < 72)
              link_h = 72;
            int text_h = ch - link_h - 8;
            if (text_h < 40)
              text_h = 40;
            int lx = cx;
            int ly = cy + text_h + 8;
            int links_y = ly + 22;
            int links_h = link_h - 24;
            int row_h = 14;
            if (mx >= lx && mx < lx + cw && my >= links_y &&
                my < links_y + links_h) {
              int rel = my - links_y;
              int idx = rel / row_h;
              if (idx >= 0 && idx < br_link_count) {
                br_open_link(idx + 1);
              }
            }

            clicked_win = 1;
            active_window_focus = 10;
          }
        }

        /* System Monitor Window */
        if (sysmon_vis && !clicked_win && top_win == 3) {
          /* Resize grip gets priority over body clicks */
          if (!sysmon_minimized && !sysmon_maximized &&
              mx >= sx + sw - RESIZE_GRIP && mx < sx + sw &&
              my >= sy + sh - RESIZE_GRIP && my < sy + sh) {
            dragging = 103; /* sysmon resize */
            resize_base_w = sw;
            resize_base_h = sh;
            resize_start_x = mx;
            resize_start_y = my;
            clicked_win = 1;
            active_window_focus = 3;
          } else if (mx >= sx && mx < sx + sw && my >= sy && my < sy + TITLE_H) {
            int x_close = sx + sw - (BTN_PAD + BTN_SZ);
            int x_max = x_close - (BTN_SZ + 4);
            int x_min = x_max - (BTN_SZ + 4);
            if (mx >= x_close && mx < x_close + BTN_SZ && my >= sy + BTN_PAD &&
                my < sy + BTN_PAD + BTN_SZ) {
              sysmon_vis = 0;
            } else if (mx >= x_max && mx < x_max + BTN_SZ &&
                       my >= sy + BTN_PAD && my < sy + BTN_PAD + BTN_SZ) {
              if (!sysmon_maximized) {
                sysmon_prev_x = sx;
                sysmon_prev_y = sy;
                sysmon_prev_w = sw;
                sysmon_prev_h = sh;
                sysmon_maximized = 1;
                sx = 0;
                sy = 0;
                sw = (int)fb_width();
                sh = (int)fb_height() - TASKBAR_H;
              } else {
                sysmon_maximized = 0;
                sx = sysmon_prev_x;
                sy = sysmon_prev_y;
                sw = sysmon_prev_w;
                sh = sysmon_prev_h;
              }
            } else if (mx >= x_min && mx < x_min + BTN_SZ && my >= sy + BTN_PAD &&
                       my < sy + BTN_PAD + BTN_SZ) {
              sysmon_minimized = 1;
            } else if (!sysmon_maximized) {
              dragging = 3;
              dox = mx - sx;
              doy = my - sy;
            }
            clicked_win = 1;
            active_window_focus = 3;
          } else if (mx >= sx && mx < sx + sw && my >= sy && my < sy + sh) {
            /* Tabs */
            if (my >= sysmon_tab_perf_y &&
                my < sysmon_tab_perf_y + sysmon_tab_perf_h) {
              if (mx >= sysmon_tab_perf_x &&
                  mx < sysmon_tab_perf_x + sysmon_tab_perf_w) {
                sysmon_tab = 0;
              } else if (mx >= sysmon_tab_proc_x &&
                         mx < sysmon_tab_proc_x + sysmon_tab_proc_w) {
                sysmon_tab = 1;
              }
            }

            if (sysmon_pager_vis && sysmon_pager_step > 0 &&
                sysmon_last_core_count > 0) {
              int in_prev =
                  (mx >= sysmon_pager_prev_x &&
                   mx < sysmon_pager_prev_x + sysmon_pager_btn_w &&
                   my >= sysmon_pager_prev_y &&
                   my < sysmon_pager_prev_y + sysmon_pager_btn_h);
              int in_next =
                  (mx >= sysmon_pager_next_x &&
                   mx < sysmon_pager_next_x + sysmon_pager_btn_w &&
                   my >= sysmon_pager_next_y &&
                   my < sysmon_pager_next_y + sysmon_pager_btn_h);

              if (in_prev) {
                sysmon_core_offset -= sysmon_pager_step;
                if (sysmon_core_offset < 0)
                  sysmon_core_offset = 0;
              } else if (in_next) {
                int next = sysmon_core_offset + sysmon_pager_step;
                if (next < sysmon_last_core_count)
                  sysmon_core_offset = next;
              }
            }

            /* Processes tab interactions: select row + End Task button */
            if (sysmon_tab == 1) {
              if (sysmon_end_btn_enabled &&
                  mx >= sysmon_end_btn_x &&
                  mx < sysmon_end_btn_x + sysmon_end_btn_w &&
                  my >= sysmon_end_btn_y &&
                  my < sysmon_end_btn_y + sysmon_end_btn_h) {
                end_task_by_pid(sysmon_selected_pid);
                sysmon_selected_pid = -1;
                sysmon_visible_pid_count = 0;
                clicked_win = 1;
                if (sysmon_vis)
                  active_window_focus = 3;
              } else if (sysmon_visible_pid_count > 0 &&
                         mx >= sysmon_proc_rows_x1 &&
                         mx < sysmon_proc_rows_x2 &&
                         my >= sysmon_proc_rows_y &&
                         my < sysmon_proc_rows_y +
                                  sysmon_visible_pid_count *
                                      sysmon_proc_rows_h) {
                int idx = (my - sysmon_proc_rows_y) / sysmon_proc_rows_h;
                if (idx >= 0 && idx < sysmon_visible_pid_count) {
                  sysmon_selected_pid = sysmon_visible_pids[idx];
                }
                clicked_win = 1;
                active_window_focus = 3;
              }
            }
            clicked_win = 1;
            if (sysmon_vis)
              active_window_focus = 3;
          }
        }

        /* About Window (z-index middle) */
        if (about_vis && !clicked_win && top_win == 2) {
          if (mx >= ax && mx < ax + aw && my >= ay && my < ay + TITLE_H) {
            int x_close = ax + aw - (BTN_PAD + BTN_SZ);
            int x_min = x_close - (BTN_SZ + 4);
            if (mx >= x_close && mx < x_close + BTN_SZ && my >= ay + BTN_PAD &&
                my < ay + BTN_PAD + BTN_SZ) {
              about_vis = 0;
            } else if (mx >= x_min && mx < x_min + BTN_SZ && my >= ay + BTN_PAD &&
                       my < ay + BTN_PAD + BTN_SZ) {
              about_minimized = 1;
            } else {
              dragging = 2;
              dox = mx - ax;
              doy = my - ay;
            }
            clicked_win = 1;
            active_window_focus = 2;
          } else if (mx >= ax && mx < ax + aw && my >= ay && my < ay + ah) {
            clicked_win = 1;
            active_window_focus = 2;
          }
        }

        /* DOOM Window */
        if (doom_running && doom_vis && !doom_minimized && !clicked_win &&
            top_win == 5) {
          int dw = doom_maximized ? (int)fb_width() : doom_w;
          int dh = doom_maximized ? ((int)fb_height() - TASKBAR_H - TITLE_H)
                                  : doom_h;
          int bx = doom_maximized ? 0 : doom_x;
          int by = doom_maximized ? 0 : doom_y;

          /* Resize grip gets priority over body clicks */
          if (!doom_maximized && mx >= bx + dw - RESIZE_GRIP && mx < bx + dw &&
              my >= by + dh + TITLE_H - RESIZE_GRIP &&
              my < by + dh + TITLE_H) {
            dragging = 105; /* doom resize */
            resize_base_w = doom_w;
            resize_base_h = doom_h;
            resize_start_x = mx;
            resize_start_y = my;
            clicked_win = 1;
            active_window_focus = 5;
          } else if (mx >= bx && mx < bx + dw && my >= by && my < by + TITLE_H) {
            if (mx >= bx + dw - 20 && mx < bx + dw - 4 && my >= by + 3 &&
                my < by + 19) {
              /* Close (Terminate DOOM) */
              doom_vis = 0;
              doom_minimized = 0;
              doom_maximized = 0;
              doom_running = 0;

              /* Unhook DOOM keys */
              extern void (*keyboard_event_hook)(int, int);
              keyboard_event_hook = 0;

              /* Kill the simulated process row immediately */
              if (_pid_doom >= 0) {
                os_process_kill(_pid_doom);
                _pid_doom = -1;
              }
            } else if (mx >= bx + dw - 40 && mx < bx + dw - 24 &&
                       my >= by + 3 && my < by + 19) {
              if (!doom_maximized) {
                doom_prev_x = doom_x;
                doom_prev_y = doom_y;
                doom_prev_w = doom_w;
                doom_prev_h = doom_h;
                doom_maximized = 1;
              } else {
                doom_maximized = 0;
                doom_x = doom_prev_x;
                doom_y = doom_prev_y;
                doom_w = doom_prev_w;
                doom_h = doom_prev_h;
              }
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
        if (win_vis && !clicked_win && top_win == 1) {
          /* Resize grip gets priority over body clicks */
          if (!win_minimized && !win_maximized && mx >= wx + ww - RESIZE_GRIP &&
              mx < wx + ww && my >= wy + wh - RESIZE_GRIP && my < wy + wh) {
            dragging = 101; /* terminal resize */
            resize_base_w = ww;
            resize_base_h = wh;
            resize_start_x = mx;
            resize_start_y = my;
            clicked_win = 1;
            active_window_focus = 1;
          } else if (!win_minimized && mx >= wx && mx < wx + ww && my >= wy &&
                     my < wy + TITLE_H) {
            int x_close = wx + ww - (BTN_PAD + BTN_SZ);
            int x_max = x_close - (BTN_SZ + 4);
            int x_min = x_max - (BTN_SZ + 4);
            if (mx >= x_close && mx < x_close + BTN_SZ && my >= wy + BTN_PAD &&
                my < wy + BTN_PAD + BTN_SZ) {
              win_vis = 0;
            } else if (mx >= x_max && mx < x_max + BTN_SZ &&
                       my >= wy + BTN_PAD && my < wy + BTN_PAD + BTN_SZ) {
              if (!win_maximized) {
                win_prev_x = wx;
                win_prev_y = wy;
                win_prev_w = ww;
                win_prev_h = wh;
                win_maximized = 1;
                wx = 0;
                wy = 0;
                ww = (int)fb_width();
                wh = (int)fb_height() - TASKBAR_H;
              } else {
                win_maximized = 0;
                wx = win_prev_x;
                wy = win_prev_y;
                ww = win_prev_w;
                wh = win_prev_h;
              }
            } else if (mx >= x_min && mx < x_min + BTN_SZ && my >= wy + BTN_PAD &&
                       my < wy + BTN_PAD + BTN_SZ) {
              win_minimized = 1;
            } else if (!win_maximized) {
              dragging = 1;
              dox = mx - wx;
              doy = my - wy;
            }
            clicked_win = 1;
            active_window_focus = 1;
          } else if (!win_minimized && mx >= wx && mx < wx + ww && my >= wy &&
                     my < wy + wh) {
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
          if (!win_vis) {
            win_vis = 1;
            win_minimized = 0;
            active_window_focus = 1;
          } else if (win_minimized) {
            win_minimized = 0;
            active_window_focus = 1;
          } else {
            win_vis = !win_vis;
            if (win_vis)
              active_window_focus = 1;
          }
        }

        /* Taskbar DOOM button */
        int x = 170;
        if (doom_running && mx >= x && mx < x + 80 && my >= ty + 4 &&
            my < ty + TASKBAR_H - 4) {
          if (doom_minimized) {
            doom_minimized = 0;
            active_window_focus = 5;
          } else {
            doom_vis = !doom_vis;
            if (doom_vis)
              active_window_focus = 5;
          }
        }

        int next_x = doom_running ? (x + 90) : x;
        const int bw_btn = 90;
        const int gap = 8;

        /* Taskbar About button */
        if (about_vis && mx >= next_x && mx < next_x + bw_btn &&
            my >= ty + 4 && my < ty + TASKBAR_H - 4) {
          if (about_minimized) {
            about_minimized = 0;
            active_window_focus = 2;
          } else if (active_window_focus != 2) {
            active_window_focus = 2;
          } else {
            about_minimized = 1;
          }
        }
        if (about_vis)
          next_x += bw_btn + gap;

        /* Taskbar Sysmon button */
        if (sysmon_vis && mx >= next_x && mx < next_x + bw_btn &&
            my >= ty + 4 && my < ty + TASKBAR_H - 4) {
          if (sysmon_minimized) {
            sysmon_minimized = 0;
            active_window_focus = 3;
          } else if (active_window_focus != 3) {
            active_window_focus = 3;
          } else {
            sysmon_minimized = 1;
          }
        }
        if (sysmon_vis)
          next_x += bw_btn + gap;

        /* Taskbar Settings button */
        if (settings_vis && mx >= next_x && mx < next_x + bw_btn &&
            my >= ty + 4 && my < ty + TASKBAR_H - 4) {
          if (settings_minimized) {
            settings_minimized = 0;
            active_window_focus = 4;
          } else if (active_window_focus != 4) {
            active_window_focus = 4;
          } else {
            settings_minimized = 1;
          }
        }
        if (settings_vis)
          next_x += bw_btn + gap;

        /* Taskbar Benchmark button */
        if (bench_vis && mx >= next_x && mx < next_x + bw_btn &&
            my >= ty + 4 && my < ty + TASKBAR_H - 4) {
          if (bench_minimized) {
            bench_minimized = 0;
            active_window_focus = 6;
          } else if (active_window_focus != 6) {
            active_window_focus = 6;
          } else {
            bench_minimized = 1;
          }
        }
        if (bench_vis)
          next_x += bw_btn + gap;

        /* Taskbar Files button */
        if (files_vis && mx >= next_x && mx < next_x + bw_btn &&
            my >= ty + 4 && my < ty + TASKBAR_H - 4) {
          if (files_minimized) {
            files_minimized = 0;
            active_window_focus = 7;
          } else if (active_window_focus != 7) {
            active_window_focus = 7;
          } else {
            files_minimized = 1;
          }
        }
        if (files_vis)
          next_x += bw_btn + gap;

        /* Taskbar Calculator button */
        if (calc_vis && mx >= next_x && mx < next_x + bw_btn &&
            my >= ty + 4 && my < ty + TASKBAR_H - 4) {
          if (calc_minimized) {
            calc_minimized = 0;
            active_window_focus = 8;
          } else if (active_window_focus != 8) {
            active_window_focus = 8;
          } else {
            calc_minimized = 1;
          }
        }
        if (calc_vis)
          next_x += bw_btn + gap;

        /* Taskbar Notepad button */
        if (note_vis && mx >= next_x && mx < next_x + bw_btn &&
            my >= ty + 4 && my < ty + TASKBAR_H - 4) {
          if (note_minimized) {
            note_minimized = 0;
            active_window_focus = 9;
          } else if (active_window_focus != 9) {
            active_window_focus = 9;
          } else {
            note_minimized = 1;
          }
        }
        if (note_vis)
          next_x += bw_btn + gap;

        /* Taskbar Browser button */
        if (browser_vis && mx >= next_x && mx < next_x + bw_btn &&
            my >= ty + 4 && my < ty + TASKBAR_H - 4) {
          if (browser_minimized) {
            browser_minimized = 0;
            active_window_focus = 10;
          } else if (active_window_focus != 10) {
            active_window_focus = 10;
          } else {
            browser_minimized = 1;
          }
        }
        if (browser_vis)
          next_x += bw_btn + gap;
      }
    }
    if (!btn)
      dragging = 0;

    if (dragging == 1 && win_vis) {
      wx = mx - dox;
      wy = my - doy;
      clamp_window_to_desktop(&wx, &wy, ww, wh);
    } else if (dragging == 2 && about_vis) {
      ax = mx - dox;
      ay = my - doy;
      clamp_window_to_desktop(&ax, &ay, aw, ah);
    } else if (dragging == 3 && sysmon_vis) {
      sx = mx - dox;
      sy = my - doy;
      clamp_window_to_desktop(&sx, &sy, sw, sh);
    } else if (dragging == 6 && bench_vis) {
      bx = mx - dox;
      by = my - doy;
      clamp_window_to_desktop(&bx, &by, bw, bh);
    } else if (dragging == 7 && files_vis) {
      fx = mx - dox;
      fy = my - doy;
      clamp_window_to_desktop(&fx, &fy, fw, fh);
    } else if (dragging == 8 && calc_vis) {
      calcx = mx - dox;
      calcy = my - doy;
      clamp_window_to_desktop(&calcx, &calcy, calcw, calch);
    } else if (dragging == 9 && note_vis) {
      notex = mx - dox;
      notey = my - doy;
      clamp_window_to_desktop(&notex, &notey, notew, noteh);
    } else if (dragging == 10 && browser_vis) {
      brx = mx - dox;
      bry = my - doy;
      clamp_window_to_desktop(&brx, &bry, brw, brh);
    } else if (dragging == 4 && settings_vis) {
      settx = mx - dox;
      setty = my - doy;
      clamp_window_to_desktop(&settx, &setty, settw, setth);
    } else if (dragging == 5 && doom_running && doom_vis && !doom_maximized) {
      doom_x = mx - dox;
      doom_y = my - doy;
      clamp_window_to_desktop(&doom_x, &doom_y, doom_w, doom_h + TITLE_H);
    } else if (dragging == 101 && win_vis && !win_maximized) {
      int nw = resize_base_w + (mx - resize_start_x);
      int nh = resize_base_h + (my - resize_start_y);
      if (nw < 240)
        nw = 240;
      if (nh < 160)
        nh = 160;
      ww = nw;
      wh = nh;
      if (ww > (int)fb_width())
        ww = (int)fb_width();
      if (wh > (int)fb_height() - TASKBAR_H)
        wh = (int)fb_height() - TASKBAR_H;
      clamp_window_to_desktop(&wx, &wy, ww, wh);
    } else if (dragging == 102 && about_vis && !about_maximized) {
      int nw = resize_base_w + (mx - resize_start_x);
      int nh = resize_base_h + (my - resize_start_y);
      if (nw < 260)
        nw = 260;
      if (nh < 160)
        nh = 160;
      aw = nw;
      ah = nh;
      if (aw > (int)fb_width())
        aw = (int)fb_width();
      if (ah > (int)fb_height() - TASKBAR_H)
        ah = (int)fb_height() - TASKBAR_H;
      clamp_window_to_desktop(&ax, &ay, aw, ah);
    } else if (dragging == 103 && sysmon_vis && !sysmon_maximized) {
      int nw = resize_base_w + (mx - resize_start_x);
      int nh = resize_base_h + (my - resize_start_y);
      if (nw < 360)
        nw = 360;
      if (nh < 220)
        nh = 220;
      sw = nw;
      sh = nh;
      if (sw > (int)fb_width())
        sw = (int)fb_width();
      if (sh > (int)fb_height() - TASKBAR_H)
        sh = (int)fb_height() - TASKBAR_H;
      clamp_window_to_desktop(&sx, &sy, sw, sh);
    } else if (dragging == 104 && settings_vis && !settings_maximized) {
      int nw = resize_base_w + (mx - resize_start_x);
      int nh = resize_base_h + (my - resize_start_y);
      if (nw < 340)
        nw = 340;
      if (nh < 220)
        nh = 220;
      settw = nw;
      setth = nh;
      if (settw > (int)fb_width())
        settw = (int)fb_width();
      if (setth > (int)fb_height() - TASKBAR_H)
        setth = (int)fb_height() - TASKBAR_H;
      clamp_window_to_desktop(&settx, &setty, settw, setth);
    } else if (dragging == 106 && bench_vis && !bench_maximized) {
      int nw = resize_base_w + (mx - resize_start_x);
      int nh = resize_base_h + (my - resize_start_y);
      if (nw < 360)
        nw = 360;
      if (nh < 240)
        nh = 240;
      bw = nw;
      bh = nh;
      if (bw > (int)fb_width())
        bw = (int)fb_width();
      if (bh > (int)fb_height() - TASKBAR_H)
        bh = (int)fb_height() - TASKBAR_H;
      clamp_window_to_desktop(&bx, &by, bw, bh);
    } else if (dragging == 107 && files_vis && !files_maximized) {
      int nw = resize_base_w + (mx - resize_start_x);
      int nh = resize_base_h + (my - resize_start_y);
      if (nw < 380)
        nw = 380;
      if (nh < 260)
        nh = 260;
      fw = nw;
      fh = nh;
      if (fw > (int)fb_width())
        fw = (int)fb_width();
      if (fh > (int)fb_height() - TASKBAR_H)
        fh = (int)fb_height() - TASKBAR_H;
      clamp_window_to_desktop(&fx, &fy, fw, fh);
    } else if (dragging == 108 && calc_vis && !calc_maximized) {
      int nw = resize_base_w + (mx - resize_start_x);
      int nh = resize_base_h + (my - resize_start_y);
      if (nw < 240)
        nw = 240;
      if (nh < 260)
        nh = 260;
      calcw = nw;
      calch = nh;
      if (calcw > (int)fb_width())
        calcw = (int)fb_width();
      if (calch > (int)fb_height() - TASKBAR_H)
        calch = (int)fb_height() - TASKBAR_H;
      clamp_window_to_desktop(&calcx, &calcy, calcw, calch);
    } else if (dragging == 109 && note_vis && !note_maximized) {
      int nw = resize_base_w + (mx - resize_start_x);
      int nh = resize_base_h + (my - resize_start_y);
      if (nw < 320)
        nw = 320;
      if (nh < 220)
        nh = 220;
      notew = nw;
      noteh = nh;
      if (notew > (int)fb_width())
        notew = (int)fb_width();
      if (noteh > (int)fb_height() - TASKBAR_H)
        noteh = (int)fb_height() - TASKBAR_H;
      clamp_window_to_desktop(&notex, &notey, notew, noteh);
    } else if (dragging == 110 && browser_vis && !browser_maximized) {
      int nw = resize_base_w + (mx - resize_start_x);
      int nh = resize_base_h + (my - resize_start_y);
      if (nw < 360)
        nw = 360;
      if (nh < 240)
        nh = 240;
      brw = nw;
      brh = nh;
      if (brw > (int)fb_width())
        brw = (int)fb_width();
      if (brh > (int)fb_height() - TASKBAR_H)
        brh = (int)fb_height() - TASKBAR_H;
      clamp_window_to_desktop(&brx, &bry, brw, brh);
    } else if (dragging == 105 && doom_running && doom_vis && !doom_maximized) {
      int nw = resize_base_w + (mx - resize_start_x);
      int nh = resize_base_h + (my - resize_start_y);
      if (nw < 240)
        nw = 240;
      if (nh < 160)
        nh = 160;
      doom_w = nw;
      doom_h = nh;
      if (doom_w > (int)fb_width())
        doom_w = (int)fb_width();
      int max_doom_h = (int)fb_height() - TASKBAR_H - TITLE_H;
      if (max_doom_h < 0)
        max_doom_h = 0;
      if (doom_h > max_doom_h)
        doom_h = max_doom_h;
      clamp_window_to_desktop(&doom_x, &doom_y, doom_w, doom_h + TITLE_H);
    }
    prev_btn = btn;

    /* Keyboard */
    while (keyboard_has_char()) {
      char c = keyboard_getchar();
      if (browser_vis && !browser_minimized && active_window_focus == 10) {
        br_handle_key(c);
      } else if (calc_vis && !calc_minimized && active_window_focus == 8) {
        calc_handle_key(c);
      } else if (note_vis && !note_minimized && active_window_focus == 9) {
        if (note_prompt_vis)
          note_prompt_handle_key(c);
        else
          note_handle_key(c);
      } else if (files_vis && !files_minimized && active_window_focus == 7 &&
          fm_prompt_vis) {
        fm_prompt_handle_key(c);
      } else if (win_vis && !win_minimized && active_window_focus == 1)
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
