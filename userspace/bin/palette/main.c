#include <akaos/display.h>
#include <akaos/toolkit.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <poll.h>

#define PALETTE_WIDTH 220U
#define PALETTE_HEIGHT 150U
#define PALETTE_RESIZED_WIDTH 280U
#define PALETTE_RESIZED_HEIGHT 180U

struct client_surface {
    char shm_name[32];
    size_t mapped_size;
    struct akaos_surface_header *header;
    uint32_t *pixels;
};

static void draw_rect(uint32_t *pixels, uint32_t stride,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color) {
    for (uint32_t row = 0; row < h; ++row) {
        if (y + row >= PALETTE_HEIGHT) break;
        for (uint32_t col = 0; col < w; ++col) {
            if (x + col >= PALETTE_WIDTH) break;
            pixels[(y + row) * stride + (x + col)] = color;
        }
    }
}

static void draw_palette_ui(uint32_t *pixels, uint32_t stride) {
    /* Window background */
    draw_rect(pixels, stride, 0, 0, PALETTE_WIDTH, PALETTE_HEIGHT, UINT32_C(0x181c24));

    /* Border */
    draw_rect(pixels, stride, 0, 0, PALETTE_WIDTH, 1, UINT32_C(0x3a4556));
    draw_rect(pixels, stride, 0, PALETTE_HEIGHT - 1, PALETTE_WIDTH, 1, UINT32_C(0x3a4556));
    draw_rect(pixels, stride, 0, 0, 1, PALETTE_HEIGHT, UINT32_C(0x3a4556));
    draw_rect(pixels, stride, PALETTE_WIDTH - 1, 0, 1, PALETTE_HEIGHT, UINT32_C(0x3a4556));

    /* Header banner */
    draw_rect(pixels, stride, 4, 4, PALETTE_WIDTH - 8, 16, UINT32_C(0x232c3d));

    /* Color swatches */
    static const uint32_t swatches[] = {
        UINT32_C(0xe06c75), /* Red */
        UINT32_C(0x98c379), /* Green */
        UINT32_C(0x61afef), /* Blue */
        UINT32_C(0xe5c07b), /* Yellow */
        UINT32_C(0xc678dd), /* Purple */
        UINT32_C(0x56b6c2), /* Cyan */
        UINT32_C(0xd19a66), /* Orange */
        UINT32_C(0xabb2bf), /* Silver */
        UINT32_C(0xbe5046), /* Dark Red */
        UINT32_C(0x7fb347), /* Lime */
        UINT32_C(0x4078f2), /* Azure */
        UINT32_C(0xa626a4)  /* Magenta */
    };
    size_t swatch_count = sizeof(swatches) / sizeof(swatches[0]);
    uint32_t swatch_w = 28;
    uint32_t swatch_h = 20;

    for (size_t i = 0; i < swatch_count; ++i) {
        uint32_t col = (uint32_t)(i % 6);
        uint32_t row = (uint32_t)(i / 6);
        uint32_t sx = 10 + col * (swatch_w + 6);
        uint32_t sy = 26 + row * (swatch_h + 6);
        /* Swatch border */
        draw_rect(pixels, stride, sx - 1, sy - 1, swatch_w + 2, swatch_h + 2, UINT32_C(0x4b5263));
        /* Swatch fill */
        draw_rect(pixels, stride, sx, sy, swatch_w, swatch_h, swatches[i]);
    }

    /* Color spectrum / gradient bar */
    uint32_t bar_x = 10;
    uint32_t bar_y = 80;
    uint32_t bar_w = PALETTE_WIDTH - 20;
    uint32_t bar_h = 14;
    draw_rect(pixels, stride, bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, UINT32_C(0x4b5263));
    for (uint32_t x = 0; x < bar_w; ++x) {
        uint32_t r = (x * 255) / bar_w;
        uint32_t g = ((bar_w - x) * 255) / bar_w;
        uint32_t b = ((x * 2) % 256);
        uint32_t c = (r << 16) | (g << 8) | b;
        for (uint32_t y = 0; y < bar_h; ++y) {
            pixels[(bar_y + y) * stride + (bar_x + x)] = c;
        }
    }

    /* Canvas preview box with geometric shapes */
    uint32_t box_x = 10;
    uint32_t box_y = 102;
    uint32_t box_w = PALETTE_WIDTH - 20;
    uint32_t box_h = 40;
    draw_rect(pixels, stride, box_x, box_y, box_w, box_h, UINT32_C(0x12151c));
    draw_rect(pixels, stride, box_x - 1, box_y - 1, box_w + 2, 1, UINT32_C(0x282c34));
    draw_rect(pixels, stride, box_x - 1, box_y + box_h, box_w + 2, 1, UINT32_C(0x282c34));
    draw_rect(pixels, stride, box_x - 1, box_y - 1, 1, box_h + 2, UINT32_C(0x282c34));
    draw_rect(pixels, stride, box_x + box_w, box_y - 1, 1, box_h + 2, UINT32_C(0x282c34));

    /* Mini shapes inside canvas */
    draw_rect(pixels, stride, box_x + 10, box_y + 8, 24, 24, UINT32_C(0x61afef));
    draw_rect(pixels, stride, box_x + 44, box_y + 12, 36, 16, UINT32_C(0x98c379));
    draw_rect(pixels, stride, box_x + 90, box_y + 6, 28, 28, UINT32_C(0xe06c75));
    draw_rect(pixels, stride, box_x + 130, box_y + 10, 56, 20, UINT32_C(0xe5c07b));
}

static int create_palette_surface_size(struct client_surface *surf, uint32_t pid,
                                       uint32_t width, uint32_t height) {
    if (!surf) return 0;
    if (width == 0 || height == 0) return 0;
    char name[32];
    snprintf(name, sizeof(name), "/palette-surf-%u", (unsigned int)pid);
    strncpy(surf->shm_name, name, sizeof(surf->shm_name));

    size_t pixel_bytes = (size_t)width * height * sizeof(uint32_t);
    size_t content_size = sizeof(struct akaos_surface_header) + pixel_bytes;
    size_t mapped_size = (content_size + 4095U) & ~(size_t)4095U;
    surf->mapped_size = mapped_size;

    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 && errno == EEXIST) {
        (void)shm_unlink(name);
        fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    }
    if (fd < 0) return 0;
    if (ftruncate(fd, (off_t)mapped_size) != 0) {
        close(fd);
        shm_unlink(name);
        return 0;
    }

    struct akaos_surface_header *header = mmap(
        NULL, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (header == MAP_FAILED) {
        shm_unlink(name);
        return 0;
    }

    header->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    header->width = width;
    header->height = height;
    header->stride_pixels = width;
    header->format = AKAOS_SURFACE_FORMAT_XRGB8888;
    header->pixel_offset = sizeof(*header);
    header->byte_size = content_size;

    surf->header = header;
    surf->pixels = (uint32_t *)((uint8_t *)header + header->pixel_offset);

    if (width == PALETTE_WIDTH && height == PALETTE_HEIGHT) {
        draw_palette_ui(surf->pixels, width);
    } else {
        /* A resized client provides a native-size buffer; the compositor must
         * present these pixels 1:1 instead of scaling the old surface. */
        for (size_t index = 0; index < (size_t)width * height; ++index)
            surf->pixels[index] = UINT32_C(0x202938);
    }
    return 1;
}

static int create_palette_surface(struct client_surface *surf, uint32_t pid) {
    return create_palette_surface_size(surf, pid, PALETTE_WIDTH, PALETTE_HEIGHT);
}

static void destroy_palette_surface(struct client_surface *surf) {
    if (!surf) return;
    if (surf->header && surf->header != MAP_FAILED) {
        munmap(surf->header, surf->mapped_size);
        surf->header = NULL;
    }
    if (surf->shm_name[0]) {
        shm_unlink(surf->shm_name);
        surf->shm_name[0] = '\0';
    }
}

static int palette_self_test(void) {
    uint32_t pid = (uint32_t)getpid();
    struct client_surface surf;
    memset(&surf, 0, sizeof(surf));

    if (!create_palette_surface(&surf, pid)) {
        printf("palette: failed to create surface: %d\n", errno);
        return 1;
    }

    if (surf.header->width != PALETTE_WIDTH ||
        surf.header->height != PALETTE_HEIGHT ||
        surf.pixels[0] != UINT32_C(0x181c24)) {
        destroy_palette_surface(&surf);
        return 2;
    }

    /* Verify the bounded compositor queue when a compositor is available. */
    int ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0666);
    if (ipc_fd >= 0) {
        struct akaos_compositor_channel *channel = mmap(
            NULL, sizeof(*channel), PROT_READ | PROT_WRITE, MAP_SHARED, ipc_fd, 0);
        if (channel != MAP_FAILED &&
            channel->abi_version == AKAOS_DISPLAY_ABI_VERSION &&
            channel->protocol_version == AKAOS_COMPOSITOR_PROTOCOL_VERSION) {
            struct akaos_display_message received;
            struct akaos_toolkit_surface toolkit_surface;
            int attached = akaos_toolkit_surface_attach(
                &toolkit_surface, channel, pid,
                50 + (int32_t)(pid % 4U) * 20,
                50 + (int32_t)(pid % 4U) * 20,
                PALETTE_WIDTH, PALETTE_HEIGHT, surf.shm_name,
                "Palette", "palette");
            if (!attached ||
                !akaos_compositor_receive(channel, &received) ||
                received.type != AKAOS_DISPLAY_MSG_ATTACH ||
                received.surface_id != pid ||
                received.width != PALETTE_WIDTH ||
                received.height != PALETTE_HEIGHT ||
                !akaos_toolkit_surface_destroy(&toolkit_surface) ||
                !akaos_compositor_receive(channel, &received) ||
                received.type != AKAOS_DISPLAY_MSG_DESTROY) {
                munmap(channel, sizeof(*channel));
                close(ipc_fd);
                destroy_palette_surface(&surf);
                return 3;
            }
            munmap(channel, sizeof(*channel));
        }
        close(ipc_fd);
    }

    destroy_palette_surface(&surf);
    puts("userspace palette client verified");
    return 0;
}

int main(int argc, char **argv) {
    int resize_test = 0;
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: palette [--self-test|--resize-test]\nStandalone akaOS graphical palette application.");
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        return palette_self_test();
    }
    if (argc == 2 && strcmp(argv[1], "--resize-test") == 0) {
        resize_test = 1;
    }
    if (argc > 1 && !resize_test) {
        puts("palette: invalid option");
        return 1;
    }

    uint32_t pid = (uint32_t)getpid();
    struct client_surface surf;
    memset(&surf, 0, sizeof(surf));

    if (!create_palette_surface(&surf, pid)) {
        printf("palette: failed to create shared surface: %d\n", errno);
        return 1;
    }

    /* Connect to compositor channel */
    int ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0666);
    if (ipc_fd < 0) {
        printf("palette: running standalone (no active compositor at %s)\n",
               AKAOS_COMPOSITOR_IPC_PATH);
        destroy_palette_surface(&surf);
        return 0;
    }

    struct akaos_compositor_channel *channel = mmap(
        NULL, sizeof(*channel), PROT_READ | PROT_WRITE, MAP_SHARED, ipc_fd, 0);
    close(ipc_fd);

    if (channel == MAP_FAILED || channel->abi_version != AKAOS_DISPLAY_ABI_VERSION ||
        channel->protocol_version != AKAOS_COMPOSITOR_PROTOCOL_VERSION) {
        printf("palette: incompatible compositor channel\n");
        if (channel != MAP_FAILED) munmap(channel, sizeof(*channel));
        destroy_palette_surface(&surf);
        return 1;
    }

    struct akaos_toolkit_surface toolkit_surface;
    if (!akaos_toolkit_surface_attach(
            &toolkit_surface, channel, pid,
            50 + (int32_t)(pid % 4U) * 20,
            50 + (int32_t)(pid % 4U) * 20,
            PALETTE_WIDTH, PALETTE_HEIGHT, surf.shm_name,
            "Palette", "palette")) {
        puts("palette: compositor request queue full");
        munmap(channel, sizeof(*channel));
        destroy_palette_surface(&surf);
        return 1;
    }
    printf("palette: queued surface '%s' via compositor IPC\n", surf.shm_name);

    struct client_surface resized;
    memset(&resized, 0, sizeof(resized));
    if (resize_test) {
        if (!create_palette_surface_size(&resized, pid + 4096U,
                                         PALETTE_RESIZED_WIDTH,
                                         PALETTE_RESIZED_HEIGHT) ||
            !akaos_toolkit_surface_resize(
                &toolkit_surface, 42, 42, PALETTE_RESIZED_WIDTH,
                PALETTE_RESIZED_HEIGHT, resized.shm_name)) {
            puts("palette: resize request rejected");
            destroy_palette_surface(&resized);
            munmap(channel, sizeof(*channel));
            destroy_palette_surface(&surf);
            return 1;
        }
        puts("palette: resize configure queued");
        uint32_t configure_serial =
            akaos_toolkit_surface_configure_serial(&toolkit_surface);
        if (!akaos_toolkit_surface_wait_configure(&toolkit_surface,
                                                  configure_serial, 500U) ||
            !akaos_toolkit_surface_ack_configure(&toolkit_surface,
                                                 configure_serial) ||
            !akaos_toolkit_surface_wait_configure_ack(&toolkit_surface,
                                                      configure_serial, 500U)) {
            puts("palette: resize configure acknowledgement failed");
            destroy_palette_surface(&resized);
            munmap(channel, sizeof(*channel));
            destroy_palette_surface(&surf);
            return 1;
        }
        puts("palette: resize configure acknowledged");
    }

    /* Keep the surface alive while exercising a real damage notification. */
    (void)poll(NULL, 0, 150);
    if (resize_test) resized.pixels[0] ^= UINT32_C(0x00010101);
    else surf.pixels[0] ^= UINT32_C(0x00010101);
    uint32_t damage_width = resize_test ? PALETTE_RESIZED_WIDTH : PALETTE_WIDTH;
    uint32_t damage_height = resize_test ? PALETTE_RESIZED_HEIGHT : PALETTE_HEIGHT;
    if (akaos_toolkit_surface_damage(&toolkit_surface, 0, 0,
                                     damage_width, damage_height)) {
        puts("palette: damage update queued");
    }
    (void)poll(NULL, 0, 350);

    if (akaos_toolkit_surface_destroy(&toolkit_surface)) {
        puts("palette: destroy queued");
    }
    munmap(channel, sizeof(*channel));
    destroy_palette_surface(&resized);
    destroy_palette_surface(&surf);
    return 0;
}
