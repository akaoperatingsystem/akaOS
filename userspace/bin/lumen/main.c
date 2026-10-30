#include <akaos/display.h>
#include <akaos/input.h>
#include <akaos/syscall.h>
#include <akaos/ui_font_aa.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define DEMO_SURFACE_WIDTH 180U
#define DEMO_SURFACE_HEIGHT 110U
#define DEMO_SURFACE_NAME "/lumen-demo"
#define TITLEBAR_HEIGHT 28
#define PANEL_HEIGHT 48
#define TASK_WIDTH 42
#define TASK_GAP 8
#define LAUNCHER_WIDTH 42
#define POINTER_WIDTH 18
#define POINTER_HEIGHT 22
#define POINTER_DAMAGE_WIDTH (POINTER_WIDTH + 3)
#define POINTER_DAMAGE_HEIGHT (POINTER_HEIGHT + 3)

/* Crisp, conventional desktop arrow. '#' is the dark outline, '.' the light
 * fill, and the compositor adds a two-pixel shadow below it. */
static const char pointer_art[POINTER_HEIGHT][POINTER_WIDTH + 1] = {
    "#                 ",
    "##                ",
    "#.#               ",
    "#..#              ",
    "#...#             ",
    "#....#            ",
    "#.....#           ",
    "#......#          ",
    "#.......#         ",
    "#........#        ",
    "#.........#       ",
    "#......#####      ",
    "#...#..#          ",
    "#..# #..#         ",
    "#.#  #...#        ",
    "##   #....#       ",
    "#    #....#       ",
    "      #....#      ",
    "      #....#      ",
    "       #..#       ",
    "       #..#       ",
    "        ##        ",
};

static int g_light_theme;

static int load_light_theme(void) {
    int descriptor = open("/etc/akaos.conf", O_RDONLY);
    if (descriptor < 0) return 0;
    char config[1024];
    ssize_t size = read(descriptor, config, sizeof(config) - 1U);
    close(descriptor);
    if (size <= 0) return 0;
    config[size] = '\0';
    return strstr(config, "DESKTOP_THEME=light") != NULL;
}

struct lumen_mapped_slot {
    uint32_t surface_id;
    uint32_t observed_damage_seq;
    uint32_t layer_trace_reported;
    char shm_name[32];
    size_t mapped_size;
    struct akaos_surface_header *header;
    const uint32_t *pixels;
};

struct lumen_dirty_rect {
    int valid;
    int x;
    int y;
    int width;
    int height;
};

static void dirty_add(const struct akaos_display_info *display,
                      struct lumen_dirty_rect *dirty,
                      int x, int y, int width, int height) {
    if (!display || !dirty || width <= 0 || height <= 0) return;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > (int)display->width) width = (int)display->width - x;
    if (y + height > (int)display->height) height = (int)display->height - y;
    if (width <= 0 || height <= 0) return;
    if (!dirty->valid) {
        dirty->valid = 1;
        dirty->x = x;
        dirty->y = y;
        dirty->width = width;
        dirty->height = height;
        return;
    }
    int right = dirty->x + dirty->width;
    int bottom = dirty->y + dirty->height;
    if (x < dirty->x) dirty->x = x;
    if (y < dirty->y) dirty->y = y;
    if (x + width > right) right = x + width;
    if (y + height > bottom) bottom = y + height;
    dirty->width = right - dirty->x;
    dirty->height = bottom - dirty->y;
}

static void dirty_full(const struct akaos_display_info *display,
                       struct lumen_dirty_rect *dirty) {
    if (!display || !dirty) return;
    dirty->valid = 1;
    dirty->x = 0;
    dirty->y = 0;
    dirty->width = (int)display->width;
    dirty->height = (int)display->height;
}

static uint32_t display_pixel(const struct akaos_display_info *display,
                              uint32_t rgb) {
    if (display->pixel_format == AKAOS_DISPLAY_PIXEL_RGBX8888) {
        return (rgb & UINT32_C(0x00ff00)) |
               ((rgb & UINT32_C(0xff0000)) >> 16) |
               ((rgb & UINT32_C(0x0000ff)) << 16);
    }
    return rgb;
}

static int pointer_absolute_position(int32_t value, uint32_t extent) {
    /* QEMU VirtIO pointer devices expose the conventional 0..32767 absolute
     * range.  Clamp malformed input and round onto the exact framebuffer. */
    if (extent <= 1U) return 0;
    if (value < 0) value = 0;
    if (value > 32767) value = 32767;
    /* Supported framebuffer dimensions keep this product well inside 32 bits,
     * avoiding a freestanding 64-bit division runtime helper on ARM32. */
    return (int)(((uint32_t)value * (extent - 1U) + UINT32_C(16383)) /
                 UINT32_C(32767));
}

static int pointer_relative_delta(int32_t value) {
    /* Preserve pixel precision for small movements while making a grabbed
     * PS/2 pointer cross a large framebuffer without excessive motion. */
    int delta = (int)value;
    int magnitude = delta < 0 ? -delta : delta;
    if (magnitude > 14) return delta * 3;
    if (magnitude > 5) return delta * 2;
    return delta;
}

static uint32_t alpha_blend(uint32_t background, uint32_t foreground,
                            uint8_t alpha) {
    uint32_t inverse = 255U - alpha;
    uint32_t r = (((background >> 16) & 0xffU) * inverse +
                  ((foreground >> 16) & 0xffU) * alpha + 127U) / 255U;
    uint32_t g = (((background >> 8) & 0xffU) * inverse +
                  ((foreground >> 8) & 0xffU) * alpha + 127U) / 255U;
    uint32_t b = ((background & 0xffU) * inverse +
                  (foreground & 0xffU) * alpha + 127U) / 255U;
    return (r << 16) | (g << 8) | b;
}

/* Wayland ARGB8888 buffers use premultiplied color channels. Blend those
 * channels directly instead of multiplying them by alpha a second time. */
static uint32_t alpha_blend_premultiplied(uint32_t background,
                                          uint32_t foreground) {
    uint32_t alpha = foreground >> 24;
    uint32_t inverse = 255U - alpha;
    uint32_t r = ((foreground >> 16) & 0xffU) +
                 (((background >> 16) & 0xffU) * inverse + 127U) / 255U;
    uint32_t g = ((foreground >> 8) & 0xffU) +
                 (((background >> 8) & 0xffU) * inverse + 127U) / 255U;
    uint32_t b = (foreground & 0xffU) +
                 ((background & 0xffU) * inverse + 127U) / 255U;
    if (r > 255U) r = 255U;
    if (g > 255U) g = 255U;
    if (b > 255U) b = 255U;
    return (r << 16) | (g << 8) | b;
}

static uint32_t composite_argb_surface_pixel(
    const struct akaos_display_info *display, uint32_t background,
    uint32_t pixel) {
    uint32_t foreground = display_pixel(
        display, pixel & UINT32_C(0x00ffffff)) |
        (pixel & UINT32_C(0xff000000));
    return alpha_blend_premultiplied(background, foreground);
}

static int measure_ui_text(const char *text) {
    int width = 0;
    if (!text) return 0;
    while (*text) width += akaos_ui_font_get_advance(*text++);
    return width;
}

/* Draw one scanline of compact UI text into the compositor's working row.
 * Keeping text rasterization here means titlebars remain crisp and clipped
 * exactly like client pixels, while the framebuffer still receives only the
 * dirty span selected by render_composite_scene(). */
static void draw_ui_text_row(const struct akaos_display_info *display,
                             uint32_t *row, int scanline, int x, int top,
                             const char *text, uint32_t color, int max_width) {
    if (!display || !row || !text || max_width <= 0) return;
    int relative = scanline - top;
    if (relative < 0 || relative >= (int)AKAOS_UI_FONT_HEIGHT) return;
    uint32_t foreground = display_pixel(display, color);
    int origin_x = x;
    for (size_t index = 0; text[index] != '\0'; ++index) {
        if (origin_x >= x + max_width) break;
        for (uint32_t column = 0; column < AKAOS_UI_FONT_WIDTH; ++column) {
            uint8_t alpha = akaos_ui_font_get_alpha(
                text[index], (uint32_t)relative, column);
            if (!alpha) continue;
            int px = origin_x + (int)column;
            if (px >= 0 && px < (int)display->width && px < x + max_width) {
                row[px] = alpha_blend(row[px], foreground, alpha);
            }
        }
        origin_x += akaos_ui_font_get_advance(text[index]);
    }
}

static void mark_surface_bounds(const struct akaos_display_info *display,
                                struct lumen_dirty_rect *dirty,
                                const struct akaos_compositor_slot *slot) {
    if (!slot) return;
    int title = (slot->flags & (AKAOS_SURFACE_FLAG_DESKTOP |
                                AKAOS_SURFACE_FLAG_PANEL |
                                AKAOS_SURFACE_FLAG_POPUP |
                                AKAOS_SURFACE_FLAG_CURSOR |
                                AKAOS_SURFACE_FLAG_SUBSURFACE |
                                AKAOS_SURFACE_FLAG_LAYER_SHELL))
                    ? 0 : TITLEBAR_HEIGHT;
    dirty_add(display, dirty, slot->x, slot->y - title,
              (int)slot->width, (int)slot->height + title);
}

static void focus_surface(const struct akaos_display_info *display,
                          struct akaos_compositor_channel *channel,
                          int slot_index, uint32_t *next_z_order,
                          struct lumen_dirty_rect *dirty) {
    if (!display || !channel || slot_index < 0 ||
        slot_index >= (int)AKAOS_COMPOSITOR_MAX_SURFACES || !next_z_order ||
        !channel->surfaces[slot_index].active) return;
    for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
        if (channel->surfaces[index].active &&
            channel->surfaces[index].surface_id == channel->active_surface_id) {
            mark_surface_bounds(display, dirty, &channel->surfaces[index]);
            break;
        }
    }
    mark_surface_bounds(display, dirty, &channel->surfaces[slot_index]);
    channel->active_surface_id = channel->surfaces[slot_index].surface_id;
    channel->surfaces[slot_index].z_order = ++(*next_z_order);
    mark_surface_bounds(display, dirty, &channel->surfaces[slot_index]);
}

static void choose_active_surface(struct akaos_compositor_channel *channel) {
    if (!channel) return;
    uint32_t selected = 0;
    uint32_t selected_z = 0;
    for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
        const struct akaos_compositor_slot *slot = &channel->surfaces[index];
        if (slot->active && !(slot->flags & (AKAOS_SURFACE_FLAG_DESKTOP |
                                             AKAOS_SURFACE_FLAG_PANEL |
                                             AKAOS_SURFACE_FLAG_POPUP |
                                             AKAOS_SURFACE_FLAG_CURSOR |
                                             AKAOS_SURFACE_FLAG_SUBSURFACE |
                                             AKAOS_SURFACE_FLAG_LAYER_SHELL)) &&
            (selected == 0 || slot->z_order >= selected_z)) {
            selected = slot->surface_id;
            selected_z = slot->z_order;
        }
    }
    channel->active_surface_id = selected;
}

static int focus_taskbar_surface(const struct akaos_display_info *display,
                                 struct akaos_compositor_channel *channel,
                                 int pointer_x, int pointer_y,
                                 uint32_t *next_z_order,
                                 struct lumen_dirty_rect *dirty) {
    if (!display || !channel ||
        pointer_y < (int)display->height - PANEL_HEIGHT) return 0;
    uint32_t max_tasks = channel->shell_task_count;
    uint32_t task_count = 0;
    for (size_t index = 0;
         index < AKAOS_COMPOSITOR_MAX_SURFACES && task_count < max_tasks;
         ++index) {
        const struct akaos_compositor_slot *slot = &channel->surfaces[index];
        if (slot->active &&
            !(slot->flags & (AKAOS_SURFACE_FLAG_DESKTOP |
                             AKAOS_SURFACE_FLAG_PANEL |
                             AKAOS_SURFACE_FLAG_POPUP |
                             AKAOS_SURFACE_FLAG_CURSOR |
                             AKAOS_SURFACE_FLAG_SUBSURFACE |
                             AKAOS_SURFACE_FLAG_LAYER_SHELL))) {
            ++task_count;
        }
    }
    uint32_t dock_x = channel->shell_launcher_x;
    uint32_t task_x = dock_x + LAUNCHER_WIDTH + 8U;
    uint32_t drawn = 0;
    for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
        struct akaos_compositor_slot *slot = &channel->surfaces[index];
        if (!slot->active || (slot->flags & (AKAOS_SURFACE_FLAG_DESKTOP |
                                             AKAOS_SURFACE_FLAG_PANEL |
                                             AKAOS_SURFACE_FLAG_POPUP |
                                             AKAOS_SURFACE_FLAG_CURSOR |
                                             AKAOS_SURFACE_FLAG_SUBSURFACE |
                                             AKAOS_SURFACE_FLAG_LAYER_SHELL))) continue;
        if (drawn >= task_count) break;
        if (pointer_x >= (int)task_x && pointer_x < (int)task_x + TASK_WIDTH &&
            pointer_y >= (int)display->height - PANEL_HEIGHT) {
            if (slot->flags & AKAOS_SURFACE_FLAG_MINIMIZED) {
                slot->flags &= ~AKAOS_SURFACE_FLAG_MINIMIZED;
                mark_surface_bounds(display, dirty, slot);
            }
            focus_surface(display, channel, (int)index, next_z_order, dirty);
            return 1;
        }
        task_x += TASK_WIDTH + TASK_GAP;
        ++drawn;
    }
    return 0;
}

static struct akaos_surface_header *create_demo_surface(size_t *mapped_size) {
    size_t pixel_bytes = DEMO_SURFACE_WIDTH * DEMO_SURFACE_HEIGHT * sizeof(uint32_t);
    size_t content_size = sizeof(struct akaos_surface_header) + pixel_bytes;
    size_t size = (content_size + 4095U) & ~(size_t)4095U;
    int descriptor = shm_open(DEMO_SURFACE_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (descriptor < 0 && errno == EEXIST) {
        (void)shm_unlink(DEMO_SURFACE_NAME);
        descriptor = shm_open(DEMO_SURFACE_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    }
    if (descriptor < 0 || ftruncate(descriptor, (off_t)size) != 0) {
        if (descriptor >= 0) close(descriptor);
        return NULL;
    }
    struct akaos_surface_header *surface = mmap(
        NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    close(descriptor);
    if (surface == MAP_FAILED) return NULL;

    surface->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    surface->width = DEMO_SURFACE_WIDTH;
    surface->height = DEMO_SURFACE_HEIGHT;
    surface->stride_pixels = DEMO_SURFACE_WIDTH;
    surface->format = AKAOS_SURFACE_FORMAT_XRGB8888;
    surface->pixel_offset = sizeof(*surface);
    surface->byte_size = content_size;
    uint32_t *pixels = (uint32_t *)((uint8_t *)surface + surface->pixel_offset);
    for (uint32_t y = 0; y < DEMO_SURFACE_HEIGHT; ++y) {
        for (uint32_t x = 0; x < DEMO_SURFACE_WIDTH; ++x) {
            uint32_t border = x < 2 || y < 2 ||
                              x >= DEMO_SURFACE_WIDTH - 2 ||
                              y >= DEMO_SURFACE_HEIGHT - 2;
            pixels[y * DEMO_SURFACE_WIDTH + x] =
                border ? UINT32_C(0x61afef) : UINT32_C(0x1e222a);
        }
    }
    if (mapped_size) *mapped_size = size;
    return surface;
}

static uint32_t recount_surfaces(struct akaos_compositor_channel *channel) {
    if (!channel) return 0;
    uint32_t count = 0;
    for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
        if (channel->surfaces[i].active) ++count;
    }
    channel->surface_count = count;
    return count;
}

static int sync_client_surfaces(struct akaos_compositor_channel *channel,
                                struct lumen_mapped_slot *mapped) {
    if (!channel || !mapped) return 0;
    int changed = 0;
    for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
        struct akaos_compositor_slot *slot = &channel->surfaces[i];
        struct lumen_mapped_slot *m = &mapped[i];

        if (slot->active) {
            if (m->header == NULL || m->surface_id != slot->surface_id ||
                strcmp(m->shm_name, slot->shm_name) != 0) {
                /* Unmap old if different */
                if (m->header && m->header != MAP_FAILED) {
                    munmap(m->header, m->mapped_size);
                    m->header = NULL;
                    changed = 1;
                }
                int fd = shm_open(slot->shm_name, O_RDWR, 0600);
                if (fd >= 0) {
                    size_t pbytes = (size_t)slot->width * slot->height * sizeof(uint32_t);
                    size_t csize = sizeof(struct akaos_surface_header) + pbytes;
                    size_t msize = (csize + 4095U) & ~(size_t)4095U;
                    struct akaos_surface_header *hdr = mmap(
                        NULL, msize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                    close(fd);
                    size_t expected_payload = (size_t)slot->width * slot->height *
                                              sizeof(uint32_t);
                    size_t expected_bytes = sizeof(*hdr) + expected_payload;
                    if (hdr != MAP_FAILED &&
                        hdr->abi_version == AKAOS_DISPLAY_ABI_VERSION &&
                        hdr->width == slot->width &&
                        hdr->height == slot->height &&
                        hdr->stride_pixels == slot->width &&
                        (hdr->format == AKAOS_SURFACE_FORMAT_XRGB8888 ||
                         hdr->format == AKAOS_SURFACE_FORMAT_ARGB8888) &&
                        hdr->pixel_offset >= sizeof(*hdr) &&
                        hdr->pixel_offset <= msize &&
                        hdr->byte_size >= expected_bytes &&
                        hdr->byte_size <= (uint64_t)SIZE_MAX) {
                        m->surface_id = slot->surface_id;
                        strncpy(m->shm_name, slot->shm_name, sizeof(m->shm_name));
                        m->mapped_size = msize;
                        m->header = hdr;
                        m->pixels = (const uint32_t *)((const uint8_t *)hdr + hdr->pixel_offset);
                        m->observed_damage_seq = slot->damage_seq;
                        changed = 1;
                        if ((slot->flags & AKAOS_SURFACE_FLAG_LAYER_SHELL) &&
                            !m->layer_trace_reported) {
                            size_t nonzero_pixels = 0U;
                            size_t opaque_pixels = 0U;
                            size_t pixel_count =
                                (size_t)hdr->width * hdr->height;
                            for (size_t pixel = 0; pixel < pixel_count; ++pixel) {
                                if (m->pixels[pixel] != 0U) ++nonzero_pixels;
                                if ((m->pixels[pixel] >> 24) != 0U)
                                    ++opaque_pixels;
                            }
                            fprintf(stderr,
                                    "lumen: layer-shell mapped id=%u pos=%d,%d size=%ux%u format=%u nonzero=%zu opaque=%zu\n",
                                    slot->surface_id, slot->x, slot->y,
                                    slot->width, slot->height, hdr->format,
                                    nonzero_pixels, opaque_pixels);
                            m->layer_trace_reported = 1U;
                        }
                    } else {
                        fprintf(stderr,
                                "lumen: rejected SHM header id=%u name=%s size=%ux%u "
                                "abi=%u format=%u stride=%u offset=%u bytes=%llu\n",
                                slot->surface_id, slot->shm_name, slot->width,
                                slot->height, (unsigned)hdr->abi_version,
                                (unsigned)hdr->format, (unsigned)hdr->stride_pixels,
                                (unsigned)hdr->pixel_offset,
                                (unsigned long long)hdr->byte_size);
                        fflush(stderr);
                        m->header = NULL;
                    }
                } else {
                    fprintf(stderr,
                            "lumen: shm_open failed id=%u name=%s errno=%d\n",
                            slot->surface_id, slot->shm_name, errno);
                    fflush(stderr);
                    if ((slot->flags & AKAOS_SURFACE_FLAG_LAYER_SHELL) &&
                        !m->layer_trace_reported) {
                        fprintf(stderr,
                                "lumen: layer-shell shm_open failed id=%u name=%s errno=%d\n",
                                slot->surface_id, slot->shm_name, errno);
                        m->layer_trace_reported = 1U;
                    }
                }
            }
            if (m->header && m->observed_damage_seq != slot->damage_seq) {
                m->observed_damage_seq = slot->damage_seq;
                changed = 1;
            }
        } else {
            if (m->header && m->header != MAP_FAILED) {
                munmap(m->header, m->mapped_size);
                m->header = NULL;
                changed = 1;
                m->surface_id = 0;
                m->observed_damage_seq = 0;
                m->layer_trace_reported = 0U;
                m->shm_name[0] = '\0';
            }
        }
    }
    return changed;
}

static void copy_fixed_string(char *destination, size_t size, const char *source) {
    if (!destination || size == 0) return;
    memset(destination, 0, size);
    if (source) strncpy(destination, source, size - 1);
}

static int find_surface_slot(struct akaos_compositor_channel *channel,
                             uint32_t surface_id, uint32_t client_pid) {
    if (!channel) return -1;
    for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
        struct akaos_compositor_slot *slot = &channel->surfaces[i];
        if (slot->active && slot->surface_id == surface_id &&
            (client_pid == 0 || slot->client_pid == client_pid)) return (int)i;
    }
    return -1;
}

static int apply_compositor_message(const struct akaos_display_info *display,
                                    struct akaos_compositor_channel *channel,
                                    const struct akaos_display_message *message,
                                    struct lumen_dirty_rect *dirty) {
    if (!channel || !message || message->surface_id == 0) return 0;
    int slot_index = find_surface_slot(channel, message->surface_id, message->client_pid);
    switch (message->type) {
        case AKAOS_DISPLAY_MSG_ATTACH: {
            if (message->width == 0 || message->height == 0 || message->shm_name[0] == '\0') return 0;
            if (slot_index < 0) {
                for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
                    if (!channel->surfaces[i].active) { slot_index = (int)i; break; }
                }
            }
            if (slot_index < 0) return 0;
            struct akaos_compositor_slot *slot = &channel->surfaces[slot_index];
            if (slot->active) {
                mark_surface_bounds(display, dirty, slot);
            }
            slot->active = 0;
            slot->surface_id = message->surface_id;
            slot->client_pid = message->client_pid;
            slot->x = message->x;
            slot->y = message->y;
            slot->width = message->width;
            slot->height = message->height;
            slot->z_order = message->z_order;
            slot->flags = message->flags;
            slot->parent_surface_id = message->parent_surface_id;
            slot->damage_x = message->damage_x;
            slot->damage_y = message->damage_y;
            slot->damage_w = message->damage_w ? message->damage_w : message->width;
            slot->damage_h = message->damage_h ? message->damage_h : message->height;
            slot->damage_seq = 1;
            slot->configure_serial = 0;
            slot->configure_ack = 0;
            copy_fixed_string(slot->shm_name, sizeof(slot->shm_name), message->shm_name);
            copy_fixed_string(slot->title, sizeof(slot->title), message->title);
            copy_fixed_string(slot->icon_name, sizeof(slot->icon_name), message->icon_name);
            __atomic_thread_fence(__ATOMIC_RELEASE);
            slot->active = 1;
            recount_surfaces(channel);
            fprintf(stderr, "lumen: surface attached id=%u name=%s size=%ux%u at %d,%d\n",
                    slot->surface_id, slot->shm_name, slot->width, slot->height,
                    slot->x, slot->y);
            fflush(stderr);
            if (channel->active_surface_id == 0 &&
                !(slot->flags & (AKAOS_SURFACE_FLAG_POPUP |
                                 AKAOS_SURFACE_FLAG_CURSOR |
                                 AKAOS_SURFACE_FLAG_SUBSURFACE |
                                 AKAOS_SURFACE_FLAG_DESKTOP |
                                 AKAOS_SURFACE_FLAG_PANEL |
                                 AKAOS_SURFACE_FLAG_LAYER_SHELL |
                                 AKAOS_SURFACE_FLAG_MINIMIZED)))
                channel->active_surface_id = slot->surface_id;
            mark_surface_bounds(display, dirty, slot);
            return 1;
        }
        case AKAOS_DISPLAY_MSG_DAMAGE:
        case AKAOS_DISPLAY_MSG_MOVE: {
            if (slot_index < 0) return 0;
            struct akaos_compositor_slot *slot = &channel->surfaces[slot_index];
            if (message->type == AKAOS_DISPLAY_MSG_MOVE) {
                mark_surface_bounds(display, dirty, slot);
                slot->x = message->x;
                slot->y = message->y;
                mark_surface_bounds(display, dirty, slot);
            } else {
                dirty_add(display, dirty, slot->x + (int)message->damage_x,
                          slot->y + (int)message->damage_y,
                          (int)(message->damage_w ? message->damage_w : slot->width),
                          (int)(message->damage_h ? message->damage_h : slot->height));
            }
            uint32_t width = message->damage_w ? message->damage_w : slot->width;
            uint32_t height = message->damage_h ? message->damage_h : slot->height;
            akaos_compositor_damage(slot, message->damage_x, message->damage_y, width, height);
            return 1;
        }
        case AKAOS_DISPLAY_MSG_RESIZE: {
            if (slot_index < 0 || message->width == 0 || message->height == 0 ||
                message->shm_name[0] == '\0') return 0;
            struct akaos_compositor_slot *slot = &channel->surfaces[slot_index];
            /* Keep the old and new rectangles dirty so a resize never leaves
             * stale client pixels around the newly configured buffer. */
            mark_surface_bounds(display, dirty, slot);
            slot->x = message->x;
            slot->y = message->y;
            slot->width = message->width;
            slot->height = message->height;
            uint32_t minimized = slot->flags & AKAOS_SURFACE_FLAG_MINIMIZED;
            slot->flags = (message->flags & ~AKAOS_SURFACE_FLAG_MINIMIZED) |
                          minimized;
            slot->parent_surface_id = message->parent_surface_id;
            slot->damage_x = message->damage_x;
            slot->damage_y = message->damage_y;
            slot->damage_w = message->damage_w ? message->damage_w : message->width;
            slot->damage_h = message->damage_h ? message->damage_h : message->height;
            slot->damage_seq = 1U;
            uint32_t serial = message->configure_serial;
            if (serial == 0) serial = slot->configure_serial + 1U;
            if (serial == 0) serial = 1U;
            slot->configure_serial = serial;
            slot->configure_ack = 0;
            copy_fixed_string(slot->shm_name, sizeof(slot->shm_name),
                              message->shm_name);
            mark_surface_bounds(display, dirty, slot);
            return 1;
        }
        case AKAOS_DISPLAY_MSG_CONFIGURE_ACK: {
            if (slot_index < 0 || message->configure_serial == 0) return 0;
            struct akaos_compositor_slot *slot = &channel->surfaces[slot_index];
            if (slot->configure_serial != message->configure_serial) return 0;
            slot->configure_ack = message->configure_serial;
            return 1;
        }
        case AKAOS_DISPLAY_MSG_STACK: {
            if (slot_index < 0) return 0;
            struct akaos_compositor_slot *slot = &channel->surfaces[slot_index];
            mark_surface_bounds(display, dirty, slot);
            slot->z_order = message->z_order;
            slot->parent_surface_id = message->parent_surface_id;
            slot->flags = (slot->flags &
                ~AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE) |
                (message->flags & AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE);
            mark_surface_bounds(display, dirty, slot);
            return 1;
        }
        case AKAOS_DISPLAY_MSG_STATE: {
            if (slot_index < 0) return 0;
            struct akaos_compositor_slot *slot = &channel->surfaces[slot_index];
            mark_surface_bounds(display, dirty, slot);
            slot->x = message->x;
            slot->y = message->y;
            uint32_t state_mask = AKAOS_SURFACE_FLAG_MAXIMIZED |
                                  AKAOS_SURFACE_FLAG_FULLSCREEN |
                                  AKAOS_SURFACE_FLAG_LAYER_SHELL |
                                  AKAOS_SURFACE_LAYER_MASK;
            slot->flags = (slot->flags & ~state_mask) |
                          (message->flags & state_mask);
            if (message->flags & AKAOS_SURFACE_FLAG_MINIMIZED)
                slot->flags |= AKAOS_SURFACE_FLAG_MINIMIZED;
            mark_surface_bounds(display, dirty, slot);
            if ((slot->flags & AKAOS_SURFACE_FLAG_MINIMIZED) &&
                channel->active_surface_id == slot->surface_id) {
                choose_active_surface(channel);
                for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES;
                     ++index) {
                    if (channel->surfaces[index].active &&
                        channel->surfaces[index].surface_id ==
                            channel->active_surface_id) {
                        mark_surface_bounds(display, dirty,
                                            &channel->surfaces[index]);
                        break;
                    }
                }
            }
            return 1;
        }
        case AKAOS_DISPLAY_MSG_FOCUS:
            if (slot_index < 0) return 0;
            /* Repaint both sides of a focus transition.  Without the old
             * bounds, an inactive titlebar can retain its focused colour
             * until an unrelated damage event arrives. */
            if (channel->active_surface_id != message->surface_id) {
                for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
                    if (channel->surfaces[index].active &&
                        channel->surfaces[index].surface_id == channel->active_surface_id) {
                        mark_surface_bounds(display, dirty, &channel->surfaces[index]);
                        break;
                    }
                }
            }
            channel->active_surface_id = message->surface_id;
            channel->surfaces[slot_index].z_order = message->z_order;
            mark_surface_bounds(display, dirty, &channel->surfaces[slot_index]);
            return 1;
        case AKAOS_DISPLAY_MSG_DESTROY:
            if (slot_index < 0) return 0;
            mark_surface_bounds(display, dirty, &channel->surfaces[slot_index]);
            channel->surfaces[slot_index].active = 0;
            if (channel->active_surface_id == message->surface_id) {
                choose_active_surface(channel);
                for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
                    if (channel->surfaces[index].active &&
                        channel->surfaces[index].surface_id == channel->active_surface_id) {
                        mark_surface_bounds(display, dirty, &channel->surfaces[index]);
                        break;
                    }
                }
            }
            recount_surfaces(channel);
            return 1;
        default:
            return 0;
    }
}

static int drain_compositor_requests(const struct akaos_display_info *display,
                                     struct akaos_compositor_channel *channel,
                                     struct lumen_dirty_rect *dirty) {
    if (!channel) return 0;
    int changed = 0;
    struct akaos_display_message message;
    while (akaos_compositor_receive(channel, &message)) {
        if (apply_compositor_message(display, channel, &message, dirty)) changed = 1;
    }
    return changed;
}

static int compositor_surface_layer(uint32_t flags) {
    if (flags & AKAOS_SURFACE_FLAG_DESKTOP) return 0;
    if (flags & AKAOS_SURFACE_FLAG_LAYER_SHELL) {
        switch ((flags & AKAOS_SURFACE_LAYER_MASK) >>
                AKAOS_SURFACE_LAYER_SHIFT) {
            case 0U: return 1; /* background */
            case 1U: return 2; /* bottom */
            case 2U: return 4; /* top */
            default: return 5; /* overlay */
        }
    }
    if (flags & AKAOS_SURFACE_FLAG_FULLSCREEN) return 3;
    if (flags & AKAOS_SURFACE_FLAG_PANEL) return 4;
    if (flags & AKAOS_SURFACE_FLAG_POPUP) return 6;
    if (flags & AKAOS_SURFACE_FLAG_CURSOR) return 7;
    return 3;
}

static void append_surface_tree(
    const struct akaos_compositor_channel *channel,
    uint32_t surface_index, uint32_t active_mask,
    uint32_t ordered[AKAOS_COMPOSITOR_MAX_SURFACES],
    size_t *ordered_count, uint32_t *visited_mask) {
    if (!channel || !ordered || !ordered_count || !visited_mask ||
        surface_index >= AKAOS_COMPOSITOR_MAX_SURFACES ||
        !(active_mask & (UINT32_C(1) << surface_index)) ||
        (*visited_mask & (UINT32_C(1) << surface_index)))
        return;

    *visited_mask |= UINT32_C(1) << surface_index;
    const struct akaos_compositor_slot *parent =
        &channel->surfaces[surface_index];
    uint32_t below[AKAOS_COMPOSITOR_MAX_SURFACES];
    uint32_t above[AKAOS_COMPOSITOR_MAX_SURFACES];
    size_t below_count = 0U;
    size_t above_count = 0U;
    for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
        if (!(active_mask & (UINT32_C(1) << index)) || index == surface_index)
            continue;
        const struct akaos_compositor_slot *child = &channel->surfaces[index];
        if (!(child->flags & AKAOS_SURFACE_FLAG_SUBSURFACE) ||
            child->parent_surface_id != parent->surface_id ||
            child->client_pid != parent->client_pid)
            continue;
        uint32_t *group =
            (child->flags & AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE)
                ? above : below;
        size_t *group_count =
            (child->flags & AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE)
                ? &above_count : &below_count;
        size_t position = *group_count;
        while (position > 0U &&
               channel->surfaces[group[position - 1U]].z_order >
                   child->z_order) {
            group[position] = group[position - 1U];
            --position;
        }
        group[position] = (uint32_t)index;
        ++(*group_count);
    }

    for (size_t index = 0; index < below_count; ++index)
        append_surface_tree(channel, below[index], active_mask, ordered,
                            ordered_count, visited_mask);
    if (*ordered_count < AKAOS_COMPOSITOR_MAX_SURFACES)
        ordered[(*ordered_count)++] = surface_index;
    for (size_t index = 0; index < above_count; ++index)
        append_surface_tree(channel, above[index], active_mask, ordered,
                            ordered_count, visited_mask);
}

static int render_composite_scene(const struct akaos_display_info *display,
                                 struct akaos_compositor_channel *channel,
                                 struct lumen_mapped_slot *mapped,
                                 const struct akaos_surface_header *fallback_demo,
                                 int pointer_x, int pointer_y,
                                 const struct lumen_dirty_rect *dirty,
                                 int output, uint32_t *row) {
    if (!display || display->stride_pixels < display->width ||
        output < 0 || !row) return 0;

    /* Sort top-level roots by compositor layer/z, then flatten each
     * subsurface tree in below-parent, parent, above-parent order. */
    uint32_t root_indices[AKAOS_COMPOSITOR_MAX_SURFACES];
    uint32_t active_indices[AKAOS_COMPOSITOR_MAX_SURFACES];
    uint32_t active_mask = 0U;
    size_t root_count = 0U;
    size_t active_count = 0U;
    int has_wayland_cursor = 0;
    int fullscreen_active = 0;
    if (channel) {
        for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
            const struct akaos_compositor_slot *slot =
                &channel->surfaces[index];
            if (!slot->active || !mapped[index].header ||
                (slot->flags & AKAOS_SURFACE_FLAG_MINIMIZED))
                continue;
            active_mask |= UINT32_C(1) << index;
            if (slot->flags & AKAOS_SURFACE_FLAG_CURSOR)
                has_wayland_cursor = 1;
            if (slot->flags & AKAOS_SURFACE_FLAG_FULLSCREEN)
                fullscreen_active = 1;
            if (slot->parent_surface_id == 0U)
                root_indices[root_count++] = (uint32_t)index;
        }
        for (size_t i = 0; i < root_count; ++i) {
            for (size_t j = i + 1U; j < root_count; ++j) {
                const struct akaos_compositor_slot *left =
                    &channel->surfaces[root_indices[i]];
                const struct akaos_compositor_slot *right =
                    &channel->surfaces[root_indices[j]];
                int left_layer = compositor_surface_layer(left->flags);
                int right_layer = compositor_surface_layer(right->flags);
                if (left_layer > right_layer ||
                    (left_layer == right_layer &&
                     left->z_order > right->z_order)) {
                    uint32_t tmp = root_indices[i];
                    root_indices[i] = root_indices[j];
                    root_indices[j] = tmp;
                }
            }
        }
        uint32_t visited_mask = 0U;
        for (size_t index = 0; index < root_count; ++index)
            append_surface_tree(channel, root_indices[index], active_mask,
                                active_indices, &active_count, &visited_mask);
        for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
            if (!(active_mask & (UINT32_C(1) << index)) ||
                (visited_mask & (UINT32_C(1) << index)))
                continue;
            append_surface_tree(channel, (uint32_t)index, active_mask,
                                active_indices, &active_count, &visited_mask);
        }
    }

    int fallback_x = display->width > DEMO_SURFACE_WIDTH
                         ? (int)(display->width - DEMO_SURFACE_WIDTH) / 2
                         : 0;
    int fallback_y = display->height > DEMO_SURFACE_HEIGHT
                         ? (int)(display->height - DEMO_SURFACE_HEIGHT) / 2
                         : 0;

    uint32_t compose_x0 = 0;
    uint32_t compose_x1 = display->stride_pixels;
    if (dirty && dirty->valid) {
        compose_x0 = (uint32_t)dirty->x;
        compose_x1 = (uint32_t)(dirty->x + dirty->width);
        if (compose_x0 > display->stride_pixels) compose_x0 = display->stride_pixels;
        if (compose_x1 > display->stride_pixels) compose_x1 = display->stride_pixels;
    }

    for (uint32_t y = 0; y < display->height; ++y) {
        /* Cursor motion normally damages only ~25 scanlines. Skipping clean
         * rows here avoids recomputing a full million-pixel desktop for every
         * input packet. */
        if (dirty && dirty->valid &&
            ((int)y < dirty->y || (int)y >= dirty->y + dirty->height)) continue;
        /* Native Aurora wallpaper.  Lumen owns this full-screen layer so a
         * 1280x800 desktop does not consume a multi-megabyte SHM object. */
        uint32_t safe_width = display->width ? display->width : 1U;
        uint32_t safe_height = display->height ? display->height : 1U;
        for (uint32_t x = compose_x0; x < compose_x1; ++x) {
            if (x >= display->width) {
                row[x] = display_pixel(display, 0);
                continue;
            }
            uint32_t r = g_light_theme ? 224U + y * 8U / safe_height
                                       : 7U + y * 7U / safe_height;
            uint32_t g = g_light_theme ? 231U + y * 7U / safe_height
                                       : 13U + y * 13U / safe_height;
            uint32_t b = g_light_theme ? 241U + y * 6U / safe_height
                                       : 27U + y * 20U / safe_height;
            uint32_t blue = x * 44U / safe_width;
            uint32_t violet = (safe_width - x) * 18U / safe_width;
            int ribbon_center = (int)(safe_width * 4U / 5U) - (int)y / 2;
            int ribbon_distance = (int)x - ribbon_center;
            if (ribbon_distance < 0) ribbon_distance = -ribbon_distance;
            uint32_t ribbon = 0;
            uint32_t ribbon_radius = safe_width / 3U;
            if ((uint32_t)ribbon_distance < ribbon_radius && ribbon_radius) {
                ribbon = (ribbon_radius - (uint32_t)ribbon_distance) * 32U /
                         ribbon_radius;
            }
            if (g_light_theme) {
                r -= (blue + violet) / 6U;
                g -= blue / 8U;
                b += ribbon / 4U;
            } else {
                r += violet + ribbon / 3U;
                g += blue / 2U + ribbon;
                b += blue + violet * 2U + ribbon * 2U;
            }
            if (r > 255U) r = 255U;
            if (g > 255U) g = 255U;
            if (b > 255U) b = 255U;
            row[x] = display_pixel(display, (r << 16) | (g << 8) | b);
        }

        /* If no client surfaces attached, render fallback demo */
        if (active_count == 0 && fallback_demo) {
            const uint32_t *demo_pixels = (const uint32_t *)(
                (const uint8_t *)fallback_demo + fallback_demo->pixel_offset);
            if ((int)y >= fallback_y &&
                (int)y < fallback_y + (int)fallback_demo->height) {
                uint32_t sy = (uint32_t)((int)y - fallback_y);
                for (uint32_t x = 0; x < fallback_demo->width; ++x) {
                    int dx = fallback_x + (int)x;
                    if (dx >= 0 && dx < (int)display->width) {
                        row[dx] = display_pixel(
                            display, demo_pixels[sy * fallback_demo->stride_pixels + x]);
                    }
                }
            }
        }

        /* Render active surfaces in z-order */
        for (size_t idx = 0; idx < active_count; ++idx) {
            uint32_t s_idx = active_indices[idx];
            const struct akaos_compositor_slot *slot = &channel->surfaces[s_idx];
            const struct lumen_mapped_slot *m = &mapped[s_idx];
            if (!m->header || !m->pixels) continue;

            int is_desktop = (slot->flags & AKAOS_SURFACE_FLAG_DESKTOP) != 0;
            int is_panel = (slot->flags & AKAOS_SURFACE_FLAG_PANEL) != 0;
            int is_layer_shell =
                (slot->flags & AKAOS_SURFACE_FLAG_LAYER_SHELL) != 0;
            int is_popup = (slot->flags & AKAOS_SURFACE_FLAG_POPUP) != 0;
            int is_cursor = (slot->flags & AKAOS_SURFACE_FLAG_CURSOR) != 0;
            int is_fullscreen =
                (slot->flags & AKAOS_SURFACE_FLAG_FULLSCREEN) != 0;
            int is_subsurface =
                (slot->flags & AKAOS_SURFACE_FLAG_SUBSURFACE) != 0;
            if (fullscreen_active && is_panel) continue;
            int is_window = !is_desktop && !is_panel && !is_layer_shell &&
                            !is_popup &&
                            !is_cursor && !is_subsurface && !is_fullscreen;

            int win_x = slot->x;
            int win_y = slot->y;
            int win_w = (int)slot->width;
            int win_h = (int)slot->height;
            int title_y = is_window ? (win_y - TITLEBAR_HEIGHT) : win_y;

            /* A restrained, even shadow separates each window from the
             * wallpaper without the heavy bevels of the old desktop. */
            if (is_window) {
                int shadow_y = title_y + 4;
                if ((int)y >= shadow_y && (int)y <= win_y + win_h + 5) {
                    int shadow_left = win_x + 4;
                    int shadow_right = win_x + win_w + 5;
                    if ((int)y >= win_y + win_h || (int)y < win_y) {
                        for (int x = shadow_left; x <= shadow_right; ++x) {
                            if (x >= 0 && x < (int)display->width)
                                row[x] = display_pixel(display, UINT32_C(0x070a12));
                        }
                    } else {
                        for (int x = shadow_right - 2; x <= shadow_right; ++x)
                            if (x >= 0 && x < (int)display->width)
                                row[x] = display_pixel(display, UINT32_C(0x070a12));
                    }
                }
            }

            /* Title bar rendering for regular windows */
            if (is_window && (int)y >= title_y && (int)y < win_y) {
                int focused = slot->surface_id == channel->active_surface_id;
                uint32_t top_color = g_light_theme
                                         ? (focused ? UINT32_C(0xfafcff) : UINT32_C(0xe8edf3))
                                         : (focused ? UINT32_C(0x253c60) : UINT32_C(0x242a35));
                uint32_t bottom_color = g_light_theme
                                            ? (focused ? UINT32_C(0xe6edf7) : UINT32_C(0xdce2e9))
                                            : (focused ? UINT32_C(0x192a47) : UINT32_C(0x1a1f28));
                uint32_t blend = (uint32_t)((int)y - title_y);
                uint32_t tb_r = ((top_color >> 16) * (TITLEBAR_HEIGHT - blend) +
                                 (bottom_color >> 16) * blend) / TITLEBAR_HEIGHT;
                uint32_t tb_g = (((top_color >> 8) & 0xffU) * (TITLEBAR_HEIGHT - blend) +
                                 ((bottom_color >> 8) & 0xffU) * blend) / TITLEBAR_HEIGHT;
                uint32_t tb_b = ((top_color & 0xffU) * (TITLEBAR_HEIGHT - blend) +
                                 (bottom_color & 0xffU) * blend) / TITLEBAR_HEIGHT;
                uint32_t tb_color = (tb_r << 16) | (tb_g << 8) | tb_b;
                for (int x = win_x; x < win_x + win_w; ++x) {
                    if (x >= 0 && x < (int)display->width) {
                        uint32_t chrome = tb_color;
                        int close_left = win_x + win_w - 30;
                        int minimize_left = win_x + win_w - 58;
                        int in_control_y = (int)y >= title_y + 4 &&
                                           (int)y < title_y + TITLEBAR_HEIGHT - 4;
                        int close_hover = pointer_x >= close_left &&
                                          pointer_x < close_left + 24 &&
                                          pointer_y >= title_y && pointer_y < win_y;
                        int minimize_hover = pointer_x >= minimize_left &&
                                             pointer_x < minimize_left + 24 &&
                                             pointer_y >= title_y && pointer_y < win_y;
                        if (in_control_y && x >= minimize_left && x < minimize_left + 24)
                            chrome = minimize_hover ? UINT32_C(0x354b68) : UINT32_C(0x29374b);
                        if (in_control_y && x >= close_left && x < close_left + 24)
                            chrome = close_hover ? UINT32_C(0xb84755) : UINT32_C(0x40303d);
                        if ((int)y == title_y + 15 && x >= minimize_left + 8 &&
                            x <= minimize_left + 15) chrome = UINT32_C(0xdce6f3);
                        int close_dx = x - (close_left + 12);
                        int close_dy = (int)y - (title_y + 14);
                        if ((close_dx == close_dy || close_dx == -close_dy) &&
                            close_dx >= -4 && close_dx <= 4)
                            chrome = UINT32_C(0xf7e8eb);
                        row[x] = display_pixel(display, chrome);
                    }
                }
                int title_width = measure_ui_text(slot->title[0] ? slot->title : "Window");
                int title_x = win_x + (win_w - title_width) / 2;
                if (title_x < win_x + 60) title_x = win_x + 60;
                draw_ui_text_row(display, row, (int)y, title_x, title_y + 5,
                                 slot->title[0] ? slot->title : "Window",
                                 g_light_theme
                                     ? (focused ? UINT32_C(0x1f2d40) : UINT32_C(0x687486))
                                     : (focused ? UINT32_C(0xf3f6fc) : UINT32_C(0x9ba7b9)),
                                 win_x + win_w - title_x - 64);
            }

            /* Window/surface client area rendering */
            if ((int)y >= win_y && (int)y < win_y + win_h) {
                uint32_t source_y = (uint32_t)((int)y - win_y);
                if (source_y < m->header->height) {
                    for (uint32_t x = 0; x < m->header->width; ++x) {
                        int dx = win_x + (int)x;
                        if (dx >= 0 && dx < (int)display->width) {
                            uint32_t pixel =
                                m->pixels[source_y * m->header->stride_pixels + x];
                            if (m->header->format ==
                                AKAOS_SURFACE_FORMAT_ARGB8888) {
                                row[dx] = composite_argb_surface_pixel(
                                    display, row[dx], pixel);
                            } else {
                                row[dx] = display_pixel(
                                    display, pixel & UINT32_C(0x00ffffff));
                            }
                        }
                    }
                }
            }

            /* Focus ring on the client edge, painted after the surface. */
            if (is_window && slot->surface_id == channel->active_surface_id &&
                (int)y >= win_y && (int)y < win_y + win_h) {
                uint32_t edge = display_pixel(display, UINT32_C(0x477ab8));
                if ((int)y == win_y || (int)y == win_y + win_h - 1) {
                    for (int x = win_x; x < win_x + win_w; ++x)
                        if (x >= 0 && x < (int)display->width) row[x] = edge;
                } else {
                    if (win_x >= 0 && win_x < (int)display->width) row[win_x] = edge;
                    if (win_x + win_w - 1 >= 0 && win_x + win_w - 1 < (int)display->width)
                        row[win_x + win_w - 1] = edge;
                }
            }
        }

        if (!has_wayland_cursor) {
            /* Keep Lumen's native arrow when the client has no cursor image. */
            int shadow_row = (int)y - pointer_y - 2;
            if (shadow_row >= 0 && shadow_row < POINTER_HEIGHT) {
                for (int px = 0; px < POINTER_WIDTH; ++px) {
                    if (pointer_art[shadow_row][px] == ' ' ||
                        pointer_art[shadow_row][px] == '\0') continue;
                    int screen_x = pointer_x + px + 2;
                    if (screen_x >= 0 && screen_x < (int)display->width)
                        row[screen_x] = display_pixel(
                            display, UINT32_C(0x111827));
                }
            }
            int cursor_row = (int)y - pointer_y;
            if (cursor_row >= 0 && cursor_row < POINTER_HEIGHT) {
                for (int px = 0; px < POINTER_WIDTH; ++px) {
                    char pixel = pointer_art[cursor_row][px];
                    if (pixel == ' ' || pixel == '\0') continue;
                    int screen_x = pointer_x + px;
                    if (screen_x >= 0 && screen_x < (int)display->width) {
                        uint32_t color = pixel == '#'
                            ? UINT32_C(0x0b0f17) : UINT32_C(0xf8fafc);
                        row[screen_x] = display_pixel(display, color);
                    }
                }
            }
        }

        /* Stream only the damaged horizontal span.  The compositor still
         * composes a complete row in memory so overlapping surfaces remain
         * correct, while /dev/fb0 receives a true offset write. */
        uint32_t write_x = 0;
        uint32_t write_width = display->stride_pixels;
        if (dirty && dirty->valid) {
            write_x = (uint32_t)dirty->x;
            write_width = (uint32_t)dirty->width;
        }
        size_t offset = 0;
        if (write_x > display->stride_pixels) write_x = display->stride_pixels;
        if (write_width > display->stride_pixels - write_x) {
            write_width = display->stride_pixels - write_x;
        }
        if (write_width == 0) continue;
        if (lseek(output, (off_t)(((uint64_t)y * display->stride_pixels + write_x) * sizeof(uint32_t)),
                  AKAOS_SEEK_SET) < 0) {
            return 0;
        }
        size_t write_bytes = (size_t)write_width * sizeof(uint32_t);
        while (offset < write_bytes) {
            size_t chunk = write_bytes - offset;
            if (chunk > 4096) chunk = 4096;
            ssize_t result = write(output,
                                   (const uint8_t *)row + write_x * sizeof(uint32_t) + offset,
                                   chunk);
            if (result <= 0) {
                return 0;
            }
            offset += (size_t)result;
        }
    }

    return 1;
}

static int protocol_self_test(void) {
    struct akaos_display_info pixel_test_display = {0};
    pixel_test_display.pixel_format = AKAOS_DISPLAY_PIXEL_BGRX8888;
    if (composite_argb_surface_pixel(&pixel_test_display,
                                     UINT32_C(0x00102030),
                                     UINT32_C(0x80800000)) !=
        UINT32_C(0x00881018))
        return 19;
    puts("Wayland premultiplied ARGB pixel composition verified");

    size_t size = 0;
    struct akaos_surface_header *surface = create_demo_surface(&size);
    if (!surface || surface->abi_version != AKAOS_DISPLAY_ABI_VERSION ||
        surface->format != AKAOS_SURFACE_FORMAT_XRGB8888) return 20;
    uint32_t *pixels = (uint32_t *)((uint8_t *)surface + surface->pixel_offset);

    /* Initialize test IPC channel */
    int ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_CREAT | O_RDWR, 0666);
    if (ipc_fd < 0 && errno == EEXIST) {
        (void)shm_unlink(AKAOS_COMPOSITOR_IPC_PATH);
        ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_CREAT | O_RDWR, 0666);
    }
    if (ipc_fd < 0) return 21;
    if (ftruncate(ipc_fd, sizeof(struct akaos_compositor_channel)) != 0) {
        close(ipc_fd);
        return 22;
    }
    struct akaos_compositor_channel *channel = mmap(
        NULL, sizeof(*channel), PROT_READ | PROT_WRITE, MAP_SHARED, ipc_fd, 0);
    close(ipc_fd);
    if (channel == MAP_FAILED) return 23;

    channel->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    channel->compositor_pid = (uint32_t)getpid();
    channel->display_width = 640;
    channel->display_height = 480;
    channel->surface_count = 0;
    channel->protocol_version = AKAOS_COMPOSITOR_PROTOCOL_VERSION;
    channel->request_lock = 0;
    channel->request_head = 0;
    channel->request_tail = 0;
    channel->wayland_input_lock = 0;
    channel->wayland_input_head = 0;
    channel->wayland_input_tail = 0;
    memset(channel->surfaces, 0, sizeof(channel->surfaces));

    /* Verify the bounded client queue independently of the direct-slot
     * compatibility path used by older graphical clients. */
    struct akaos_display_message queue_messages[3];
    memset(queue_messages, 0, sizeof(queue_messages));
    queue_messages[0].type = AKAOS_DISPLAY_MSG_ATTACH;
    queue_messages[0].surface_id = 1001;
    queue_messages[0].client_pid = (uint32_t)getpid();
    queue_messages[0].x = 8;
    queue_messages[0].y = 12;
    queue_messages[0].width = DEMO_SURFACE_WIDTH;
    queue_messages[0].height = DEMO_SURFACE_HEIGHT;
    queue_messages[0].z_order = 2;
    queue_messages[0].damage_w = DEMO_SURFACE_WIDTH;
    queue_messages[0].damage_h = DEMO_SURFACE_HEIGHT;
    strncpy(queue_messages[0].shm_name, DEMO_SURFACE_NAME, sizeof(queue_messages[0].shm_name) - 1);
    strncpy(queue_messages[0].title, "Queue Window", sizeof(queue_messages[0].title) - 1);
    queue_messages[1] = queue_messages[0];
    queue_messages[1].type = AKAOS_DISPLAY_MSG_DAMAGE;
    queue_messages[1].damage_x = 4;
    queue_messages[1].damage_y = 5;
    queue_messages[1].damage_w = 20;
    queue_messages[1].damage_h = 20;
    queue_messages[2] = queue_messages[0];
    queue_messages[2].type = AKAOS_DISPLAY_MSG_DESTROY;
    if (!akaos_compositor_submit(channel, &queue_messages[0]) ||
        !akaos_compositor_submit(channel, &queue_messages[1]) ||
        !akaos_compositor_submit(channel, &queue_messages[2])) return 24;
    for (size_t i = 0; i < 3; ++i) {
        struct akaos_display_message received;
        if (!akaos_compositor_receive(channel, &received) ||
            received.type != queue_messages[i].type ||
            received.surface_id != queue_messages[i].surface_id) return 24;
    }

    pid_t child = fork();
    if (child < 0) return 25;
    if (child == 0) {
        if (surface->width != DEMO_SURFACE_WIDTH ||
            pixels[0] != UINT32_C(0x61afef)) _exit(26);
        pixels[DEMO_SURFACE_WIDTH + 1] = UINT32_C(0x55aa77);

        /* Register surface in IPC channel */
        channel->surfaces[0].surface_id = 999;
        channel->surfaces[0].client_pid = (uint32_t)getpid();
        channel->surfaces[0].x = 10;
        channel->surfaces[0].y = 20;
        channel->surfaces[0].width = DEMO_SURFACE_WIDTH;
        channel->surfaces[0].height = DEMO_SURFACE_HEIGHT;
        channel->surfaces[0].z_order = 1;
        channel->surfaces[0].damage_seq = 1;
        strncpy(channel->surfaces[0].shm_name, DEMO_SURFACE_NAME, 31);
        strncpy(channel->surfaces[0].title, "Demo Window", 31);
        channel->surfaces[0].active = 1;
        channel->surface_count = 1;
        _exit(0);
    }

    int status = -1;
    int waited = waitpid(child, &status, 0);
    if (waited != child || status != 0 ||
        pixels[DEMO_SURFACE_WIDTH + 1] != UINT32_C(0x55aa77)) return 27;

    if (!channel->surfaces[0].active ||
        channel->surfaces[0].surface_id != 999 ||
        strcmp(channel->surfaces[0].title, "Demo Window") != 0) return 28;

    /* Clean channel */
    channel->surfaces[0].active = 0;
    munmap(channel, sizeof(*channel));
    shm_unlink(AKAOS_COMPOSITOR_IPC_PATH);

    if (munmap(surface, size) != 0 || shm_unlink(DEMO_SURFACE_NAME) != 0) return 29;
    puts("userspace display surface protocol verified");
    return 0;
}

int main(int argc, char **argv) {
    g_light_theme = load_light_theme();
    int once = 0;
    int input_test = 0;
    for (int argument = 1; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--help") == 0 ||
            strcmp(argv[argument], "-h") == 0) {
            puts("Usage: lumen [--once] [--self-test]\n"
                 "Run the akaOS display compositor.");
            return 0;
        }
        if (strcmp(argv[argument], "--self-test") == 0 && argc == 2)
            return protocol_self_test();
        if (strcmp(argv[argument], "--once") == 0) once = 1;
        else if (strcmp(argv[argument], "--input-test") == 0) input_test = 1;
        else {
            puts("lumen: invalid option or extra operand");
            return 1;
        }
    }

    int info_fd = open(AKAOS_FRAMEBUFFER_PATH, O_RDONLY);
    if (info_fd < 0) {
        printf("lumen: no active framebuffer: %d\n", errno);
        return 1;
    }
    struct akaos_display_info display;
    if (read(info_fd, &display, sizeof(display)) != (ssize_t)sizeof(display) ||
        close(info_fd) != 0 ||
        display.abi_version != AKAOS_DISPLAY_ABI_VERSION) {
        printf("lumen: invalid framebuffer descriptor\n");
        return 2;
    }

    /* Initialize shared IPC channel */
    int ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_CREAT | O_RDWR, 0666);
    if (ipc_fd < 0 && errno == EEXIST) {
        (void)shm_unlink(AKAOS_COMPOSITOR_IPC_PATH);
        ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_CREAT | O_RDWR, 0666);
    }
    if (ipc_fd < 0 || ftruncate(ipc_fd, sizeof(struct akaos_compositor_channel)) != 0) {
        if (ipc_fd >= 0) close(ipc_fd);
        printf("lumen: failed to initialize IPC channel (errno=%d)\n", errno);
        return 3;
    }
    struct akaos_compositor_channel *channel = mmap(
        NULL, sizeof(*channel), PROT_READ | PROT_WRITE, MAP_SHARED, ipc_fd, 0);
    close(ipc_fd);
    if (channel == MAP_FAILED) {
        printf("lumen: failed to map IPC channel (errno=%d)\n", errno);
        return 3;
    }

    channel->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    channel->compositor_pid = (uint32_t)getpid();
    channel->display_width = display.width;
    channel->display_height = display.height;
    channel->surface_count = 0;
    channel->active_surface_id = 0;
    channel->protocol_version = AKAOS_COMPOSITOR_PROTOCOL_VERSION;
    channel->request_lock = 0;
    channel->request_head = 0;
    channel->request_tail = 0;
    channel->wayland_input_lock = 0;
    channel->wayland_input_head = 0;
    channel->wayland_input_tail = 0;
    memset(channel->surfaces, 0, sizeof(channel->surfaces));
    memset(channel->requests, 0, sizeof(channel->requests));
    memset(channel->wayland_input, 0, sizeof(channel->wayland_input));

    struct lumen_mapped_slot mapped[AKAOS_COMPOSITOR_MAX_SURFACES];
    memset(mapped, 0, sizeof(mapped));

    /* The production compositor starts on its wallpaper, never on the old
     * centered demo rectangle.  create_demo_surface remains self-test only. */
    size_t fallback_size = 0;
    struct akaos_surface_header *fallback_demo = NULL;

    int owner = open(AKAOS_FRAMEBUFFER_PATH, O_WRONLY);
    if (owner < 0) {
        printf("lumen: framebuffer is unavailable (errno=%d)\n", errno);
        munmap(channel, sizeof(*channel));
        shm_unlink(AKAOS_COMPOSITOR_IPC_PATH);
        if (fallback_demo) {
            munmap(fallback_demo, fallback_size);
            shm_unlink(DEMO_SURFACE_NAME);
        }
        return 4;
    }
    size_t composition_row_bytes =
        (size_t)display.stride_pixels * sizeof(uint32_t);
    uint32_t *composition_row = malloc(composition_row_bytes);
    if (!composition_row) {
        printf("lumen: unable to allocate persistent composition row\n");
        close(owner);
        munmap(channel, sizeof(*channel));
        shm_unlink(AKAOS_COMPOSITOR_IPC_PATH);
        return 5;
    }

    int pointer_x = (int)display.width / 2;
    int pointer_y = (int)display.height / 2;
    int dragging_slot = -1;
    int drag_offset_x = 0;
    int drag_offset_y = 0;
    uint32_t next_z_order = 10;
    int wayland_surface_presented = 0;
    int wayland_popup_presented = 0;
    int wayland_cursor_presented = 0;
    int wayland_subsurface_presented = 0;
    int wayland_nested_subsurface_presented = 0;

    (void)drain_compositor_requests(&display, channel, NULL);
    (void)sync_client_surfaces(channel, mapped);
    if (!render_composite_scene(&display, channel, mapped, fallback_demo,
                               pointer_x, pointer_y, NULL, owner,
                               composition_row)) {
        printf("lumen: initial scene render failed\n");
        free(composition_row);
        close(owner);
        munmap(channel, sizeof(*channel));
        shm_unlink(AKAOS_COMPOSITOR_IPC_PATH);
        return 5;
    }
    puts("lumen: framebuffer compositor active");

    if (once) {
        (void)poll(NULL, 0, 1200);
    } else {
        int input = open(AKAOS_INPUT_EVENT_PATH, O_RDONLY);
        if (input < 0) {
            printf("lumen: input device unavailable (errno=%d)\n", errno);
            free(composition_row);
            close(owner);
            munmap(channel, sizeof(*channel));
            shm_unlink(AKAOS_COMPOSITOR_IPC_PATH);
            return 6;
        }
        int input_test_motion_reported = 0;
        int input_test_button_reported = 0;
        int input_test_drag_started = 0;
        int input_test_drag_moved = 0;
        for (;;) {
            struct lumen_dirty_rect dirty = {0};
            struct pollfd wait = {.fd = input, .events = POLLIN, .revents = 0};
            int poll_res = poll(&wait, 1, 8); /* responsive 125 Hz input/sync tick */
            if (poll_res < 0) {
                if (errno == EINTR) continue;
                break;
            }

            int changed = 0;
            int old_pointer_x = pointer_x;
            int old_pointer_y = pointer_y;
            if (poll_res > 0 && (wait.revents & POLLIN)) {
                struct akaos_input_event events[16];
                ssize_t bytes = read(input, events, sizeof(events));
                if (bytes > 0) {
                    size_t count = (size_t)bytes / sizeof(events[0]);
                    for (size_t index = 0; index < count; ++index) {
                        if (events[index].type == AKAOS_EV_KEY ||
                            events[index].type == AKAOS_EV_TEXT) {
                            struct akaos_shell_event shell_event;
                            shell_event.type = events[index].type == AKAOS_EV_TEXT
                                                   ? AKAOS_SHELL_EVENT_TEXT
                                                   : (events[index].code >= AKAOS_BTN_LEFT
                                                          ? AKAOS_SHELL_EVENT_BUTTON
                                                          : AKAOS_SHELL_EVENT_KEY);
                            shell_event.code = events[index].code;
                            shell_event.value = events[index].value;
                            shell_event.pointer_x = pointer_x;
                            shell_event.pointer_y = pointer_y;
                            (void)akaos_shell_event_submit(channel, &shell_event);
                        }
                        if (events[index].type == AKAOS_EV_REL &&
                            events[index].code == AKAOS_REL_X) {
                            pointer_x += pointer_relative_delta(events[index].value);
                            changed = 1;
                        } else if (events[index].type == AKAOS_EV_REL &&
                                   events[index].code == AKAOS_REL_Y) {
                            pointer_y += pointer_relative_delta(events[index].value);
                            changed = 1;
                        } else if (events[index].type == AKAOS_EV_ABS &&
                                   events[index].code == AKAOS_ABS_X) {
                            pointer_x = pointer_absolute_position(events[index].value,
                                                                  display.width);
                            changed = 1;
                        } else if (events[index].type == AKAOS_EV_ABS &&
                                   events[index].code == AKAOS_ABS_Y) {
                            pointer_y = pointer_absolute_position(events[index].value,
                                                                  display.height);
                            changed = 1;
                        } else if (events[index].type == AKAOS_EV_KEY &&
                                   events[index].code == AKAOS_BTN_LEFT) {
                            if (input_test && events[index].value == 1 &&
                                !input_test_button_reported) {
                                puts("lumen: mouse button verified");
                                input_test_button_reported = 1;
                            }
                            if (events[index].value == 1) {
                                if (focus_taskbar_surface(&display, channel, pointer_x, pointer_y,
                                                          &next_z_order, &dirty)) {
                                    changed = 1;
                                    continue;
                                }
                                /* Mouse click: check window titlebars for dragging */
                                int top_slot = -1;
                                uint32_t top_z = 0;
                                for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
                                    if (channel->surfaces[i].active &&
                                        !(channel->surfaces[i].flags &
                                          AKAOS_SURFACE_FLAG_MINIMIZED) &&
                                        !(channel->surfaces[i].flags &
                                          AKAOS_SURFACE_FLAG_CURSOR)) {
                                        struct akaos_compositor_slot *s = &channel->surfaces[i];
                                        int tx = s->x;
                                        int title_height =
                                            (s->flags & (AKAOS_SURFACE_FLAG_DESKTOP |
                                                         AKAOS_SURFACE_FLAG_PANEL |
                                                         AKAOS_SURFACE_FLAG_POPUP |
                                                         AKAOS_SURFACE_FLAG_CURSOR |
                                                         AKAOS_SURFACE_FLAG_SUBSURFACE |
                                                         AKAOS_SURFACE_FLAG_LAYER_SHELL))
                                                ? 0 : TITLEBAR_HEIGHT;
                                        int ty = s->y - title_height;
                                        int tw = (int)s->width;
                                        int th = title_height + (int)s->height;
                                        if (pointer_x >= tx && pointer_x < tx + tw &&
                                            pointer_y >= ty && pointer_y < ty + th) {
                                            if (s->z_order >= top_z) {
                                                top_z = s->z_order;
                                                top_slot = (int)i;
                                            }
                                        }
                                    }
                                }
                                if (top_slot >= 0) {
                                    struct akaos_compositor_slot *s = &channel->surfaces[top_slot];
                                    int is_window = !(s->flags &
                                        (AKAOS_SURFACE_FLAG_DESKTOP |
                                         AKAOS_SURFACE_FLAG_PANEL |
                                         AKAOS_SURFACE_FLAG_POPUP |
                                         AKAOS_SURFACE_FLAG_CURSOR |
                                         AKAOS_SURFACE_FLAG_SUBSURFACE |
                                         AKAOS_SURFACE_FLAG_LAYER_SHELL));
                                    if (is_window) {
                                        focus_surface(&display, channel, top_slot, &next_z_order, &dirty);
                                        int ty = s->y - TITLEBAR_HEIGHT;
                                        if (pointer_y >= ty && pointer_y < s->y) {
                                            int close_left = s->x + (int)s->width - 30;
                                            int minimize_left = s->x + (int)s->width - 58;
                                            if (pointer_x >= close_left &&
                                                pointer_x < close_left + 24) {
                                                mark_surface_bounds(&display, &dirty, s);
                                                (void)kill((pid_t)s->client_pid, SIGTERM);
                                                s->active = 0;
                                                recount_surfaces(channel);
                                                if (channel->active_surface_id == s->surface_id) {
                                                    choose_active_surface(channel);
                                                    for (size_t replacement = 0;
                                                         replacement < AKAOS_COMPOSITOR_MAX_SURFACES;
                                                         ++replacement) {
                                                        if (channel->surfaces[replacement].active &&
                                                            channel->surfaces[replacement].surface_id ==
                                                                channel->active_surface_id) {
                                                            mark_surface_bounds(&display, &dirty,
                                                                                &channel->surfaces[replacement]);
                                                            break;
                                                        }
                                                    }
                                                }
                                            } else if (pointer_x >= minimize_left &&
                                                       pointer_x < minimize_left + 24) {
                                                mark_surface_bounds(&display, &dirty, s);
                                                s->flags |= AKAOS_SURFACE_FLAG_MINIMIZED;
                                                if (channel->active_surface_id == s->surface_id)
                                                    choose_active_surface(channel);
                                            } else {
                                                /* Clicked titlebar: start drag */
                                                dragging_slot = top_slot;
                                                drag_offset_x = pointer_x - s->x;
                                                drag_offset_y = pointer_y - s->y;
                                                if (input_test &&
                                                    !input_test_drag_started) {
                                                    printf("lumen: titlebar drag started at %d,%d\n",
                                                           pointer_x, pointer_y);
                                                    input_test_drag_started = 1;
                                                }
                                            }
                                        }
                                    }
                                    changed = 1;
                                }
                            } else {
                                dragging_slot = -1;
                            }
                        }
                    }
                    for (size_t index = 0; index < count; ++index) {
                        if (events[index].type != AKAOS_EV_REL &&
                            events[index].type != AKAOS_EV_ABS &&
                            events[index].type != AKAOS_EV_KEY) continue;
                        struct akaos_wayland_input_event wayland_event = {
                            .type = events[index].type,
                            .code = events[index].code,
                            .value = events[index].value,
                            .pointer_x = pointer_x,
                            .pointer_y = pointer_y,
                        };
                        (void)akaos_wayland_input_submit(channel,
                                                         &wayland_event);
                    }
                }
            }

            if (pointer_x < 0) pointer_x = 0;
            if (pointer_y < 0) pointer_y = 0;
            if (pointer_x >= (int)display.width) pointer_x = (int)display.width - 1;
            if (pointer_y >= (int)display.height) pointer_y = (int)display.height - 1;
            if (pointer_x != old_pointer_x || pointer_y != old_pointer_y) {
                dirty_add(&display, &dirty, old_pointer_x, old_pointer_y,
                          POINTER_DAMAGE_WIDTH, POINTER_DAMAGE_HEIGHT);
                dirty_add(&display, &dirty, pointer_x, pointer_y,
                          POINTER_DAMAGE_WIDTH, POINTER_DAMAGE_HEIGHT);
                if (input_test && !input_test_motion_reported) {
                    puts("lumen: mouse motion verified");
                    input_test_motion_reported = 1;
                }
            }

            if (dragging_slot >= 0 && channel->surfaces[dragging_slot].active) {
                struct akaos_compositor_slot *dragged = &channel->surfaces[dragging_slot];
                int32_t old_x = dragged->x;
                int32_t old_y = dragged->y;
                dirty_add(&display, &dirty, dragged->x, dragged->y - TITLEBAR_HEIGHT,
                          (int)dragged->width, (int)dragged->height + TITLEBAR_HEIGHT);
                dragged->x = pointer_x - drag_offset_x;
                dragged->y = pointer_y - drag_offset_y;
                if (input_test && !input_test_drag_moved &&
                    (dragged->x != old_x || dragged->y != old_y)) {
                    printf("lumen: titlebar drag moved window to %d,%d\n",
                           dragged->x, dragged->y);
                    input_test_drag_moved = 1;
                }
                dirty_add(&display, &dirty, dragged->x, dragged->y - TITLEBAR_HEIGHT,
                          (int)dragged->width, (int)dragged->height + TITLEBAR_HEIGHT);
                changed = 1;
            }

            if (drain_compositor_requests(&display, channel, &dirty)) changed = 1;
            if (sync_client_surfaces(channel, mapped)) {
                changed = 1;
                if (!dirty.valid) dirty_full(&display, &dirty);
            }
            if (changed) {
                if (!render_composite_scene(&display, channel, mapped,
                                           fallback_demo, pointer_x, pointer_y,
                                           dirty.valid ? &dirty : NULL, owner,
                                           composition_row)) break;
                if (!wayland_surface_presented) {
                    for (size_t index = 0;
                         index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
                        if (!channel->surfaces[index].active ||
                            !(channel->surfaces[index].flags &
                              AKAOS_SURFACE_FLAG_WAYLAND)) continue;
                        puts("lumen: Wayland surface presented on framebuffer");
                        wayland_surface_presented = 1;
                        break;
                    }
                }
                if (!wayland_popup_presented) {
                    for (size_t index = 0;
                         index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
                        const struct akaos_compositor_slot *slot =
                            &channel->surfaces[index];
                        if (!slot->active ||
                            !(slot->flags & AKAOS_SURFACE_FLAG_WAYLAND) ||
                            !(slot->flags & AKAOS_SURFACE_FLAG_POPUP))
                            continue;
                        puts("lumen: Wayland popup surface presented on framebuffer");
                        wayland_popup_presented = 1;
                        break;
                    }
                }
                if (!wayland_cursor_presented) {
                    for (size_t index = 0;
                         index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
                        const struct akaos_compositor_slot *slot =
                            &channel->surfaces[index];
                        if (!slot->active ||
                            !(slot->flags & AKAOS_SURFACE_FLAG_WAYLAND) ||
                            !(slot->flags & AKAOS_SURFACE_FLAG_CURSOR))
                            continue;
                        puts("lumen: Wayland cursor surface presented on framebuffer");
                        wayland_cursor_presented = 1;
                        break;
                    }
                }
                if (!wayland_subsurface_presented) {
                    for (size_t index = 0;
                         index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
                        const struct akaos_compositor_slot *slot =
                            &channel->surfaces[index];
                        if (!slot->active ||
                            !(slot->flags & AKAOS_SURFACE_FLAG_WAYLAND) ||
                            !(slot->flags & AKAOS_SURFACE_FLAG_SUBSURFACE))
                            continue;
                        puts("lumen: Wayland subsurface tree presented on framebuffer");
                        wayland_subsurface_presented = 1;
                        break;
                    }
                }
                if (!wayland_nested_subsurface_presented) {
                    for (size_t child_index = 0;
                         child_index < AKAOS_COMPOSITOR_MAX_SURFACES;
                         ++child_index) {
                        const struct akaos_compositor_slot *child =
                            &channel->surfaces[child_index];
                        if (!child->active ||
                            !(child->flags & AKAOS_SURFACE_FLAG_WAYLAND) ||
                            !(child->flags & AKAOS_SURFACE_FLAG_SUBSURFACE) ||
                            child->parent_surface_id == 0U)
                            continue;
                        for (size_t parent_index = 0;
                             parent_index < AKAOS_COMPOSITOR_MAX_SURFACES;
                             ++parent_index) {
                            const struct akaos_compositor_slot *parent =
                                &channel->surfaces[parent_index];
                            if (!parent->active ||
                                parent->client_pid != child->client_pid ||
                                parent->surface_id != child->parent_surface_id ||
                                !(parent->flags & AKAOS_SURFACE_FLAG_SUBSURFACE) ||
                                parent->parent_surface_id == 0U)
                                continue;
                            puts("lumen: Wayland nested subsurface tree presented on framebuffer");
                            wayland_nested_subsurface_presented = 1;
                            break;
                        }
                        if (wayland_nested_subsurface_presented) break;
                    }
                }
            }
        }
        close(input);
    }

    free(composition_row);
    close(owner);
    (void)write(STDOUT_FILENO, "\033[2J\033[H", 7);
    if (once) puts("lumen framebuffer compositor presented");

    for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
        if (mapped[i].header && mapped[i].header != MAP_FAILED) {
            munmap(mapped[i].header, mapped[i].mapped_size);
        }
    }
    munmap(channel, sizeof(*channel));
    shm_unlink(AKAOS_COMPOSITOR_IPC_PATH);

    if (fallback_demo) {
        munmap(fallback_demo, fallback_size);
        shm_unlink(DEMO_SURFACE_NAME);
    }
    return 0;
}
