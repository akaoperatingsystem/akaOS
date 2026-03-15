/* ============================================================
 * akaOS — Kernel Main (robust boot with fallback)
 * ============================================================ */
#include "fb.h"
#include "fs.h"
#include "gui.h"
#include "idt.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
#include "shell.h"
#include "time.h"
#include "vga.h"

#include "limine.h"

extern void _start(void);

// ===== Limine Protocol Markers & Requests =====
// ALL request structs MUST be between the start/end markers in the binary.
// Limine scans this region for magic IDs to find requests.

// Start marker
__attribute__((
    used,
    section(".limine_requests_start"))) static LIMINE_REQUESTS_START_MARKER;

// Base revision
__attribute__((used,
               section(".limine_requests"))) static LIMINE_BASE_REVISION(0);

// Request structs — placed in .limine_requests so Limine can find them
__attribute__((
    used,
    section(
        ".limine_requests"))) static volatile struct limine_entry_point_request
    entry_point_request = {
        .id = LIMINE_ENTRY_POINT_REQUEST, .revision = 0, .entry = _start};

__attribute__((
    used,
    section(
        ".limine_requests"))) static volatile struct limine_framebuffer_request
    fb_request = {.id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0};

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_memmap_request
    memmap_request = {.id = LIMINE_MEMMAP_REQUEST, .revision = 0};

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_hhdm_request
    hhdm_request = {.id = LIMINE_HHDM_REQUEST, .revision = 0};

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_module_request
    module_request = {.id = LIMINE_MODULE_REQUEST, .revision = 0};

// End marker
__attribute__((
    used, section(".limine_requests_end"))) static LIMINE_REQUESTS_END_MARKER;

/* Global HHDM offset — needed to access physical memory in higher-half kernel
 */
uint64_t hhdm_offset = 0;

static int have_framebuffer = 0;
uint32_t sys_total_memory_mb = 0;

static void show_boot_screen(void) {
  vga_print_color("\n  akaOS v1.0 — x86_64\n\n", VGA_LIGHT_GREEN, VGA_BLACK);

  vga_print_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
  if (have_framebuffer)
    vga_print("Framebuffer initialized\n");
  else
    vga_print("VGA text mode (no framebuffer)\n");

  vga_print_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
  vga_print("IDT loaded, PIC remapped\n");
  vga_print_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
  vga_print("PIT timer (100 Hz)\n");
  vga_print_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
  vga_print("PS/2 keyboard ready\n");
  vga_print_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
  vga_print("Filesystem mounted\n");
}

void kernel_main(void) {
  /* Get HHDM offset first — needed for all physical memory access */
  if (hhdm_request.response != NULL) {
    hhdm_offset = hhdm_request.response->offset;
  }

  /* Extract boot modules (WAD loaded by Limine) */
  if (module_request.response != NULL &&
      module_request.response->module_count > 0) {
    struct limine_file *mod = module_request.response->modules[0];
    extern void *doom_wad_data;
    extern size_t doom_wad_size;
    doom_wad_data = mod->address;
    doom_wad_size = mod->size;
  }

  /* Try to initialize framebuffer from Limine response */
  if (fb_request.response != NULL &&
      fb_request.response->framebuffer_count > 0) {
    have_framebuffer =
        (fb_init_limine(fb_request.response->framebuffers[0]) == 0);
  }

  /* Try to initialize memory from Limine memmap */
  if (memmap_request.response != NULL) {
    uint64_t total_bytes = 0;
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
      struct limine_memmap_entry *entry = memmap_request.response->entries[i];
      if (entry->type == LIMINE_MEMMAP_USABLE) {
        total_bytes += entry->length;
      }
    }
    sys_total_memory_mb = (uint32_t)(total_bytes / 1024 / 1024);
  } else {
    sys_total_memory_mb = 128; // Fallback
  }

  /* Initialize VGA (uses framebuffer if available, text mode otherwise) */
  vga_init();

  /* Initialize interrupts */
  idt_init();

  /* Initialize PIT timer */
  timer_init();

  /* Initialize keyboard */
  keyboard_init();

  /* Initialize filesystem */
  fs_init();

  /* Show boot screen */
  show_boot_screen();

  if (have_framebuffer) {
    /* Initialize mouse (only useful with GUI) */
    mouse_init();
    mouse_set_bounds((int)fb_width(), (int)fb_height());
    vga_print_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
    vga_print("PS/2 mouse ready\n");

    /* Network init is optional — don't crash if it fails */
    if (net_init() == 0) {
      vga_print_color("  [OK] ", VGA_LIGHT_GREEN, VGA_BLACK);
      vga_print("Network (e1000) ready\n");
    }

    vga_print_color("\n  Starting desktop...\n", VGA_LIGHT_CYAN, VGA_BLACK);
    if (have_framebuffer)
      fb_flip();

    /* Brief pause */
    uint64_t start = timer_get_ticks();
    while (timer_get_ticks() - start < 150)
      asm volatile("hlt");

    /* Launch GUI */
    gui_init();
    gui_run();
  } else {
    /* No framebuffer — fallback to text-mode shell */
    vga_print_color("\n  Type 'help' for commands.\n\n", VGA_DARK_GREY,
                    VGA_BLACK);
    shell_run();
  }
}
