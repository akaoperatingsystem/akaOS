/*
 * akaOS XSCore Lumen Wayland Compositor - Render Engine Implementation
 */

#include "render.h"
#include <xscore/heap.h>
#include <xscore/serial.h>
#include <xscore/string.h>
#include <xscore/memory.h>

#define kmalloc(sz) xscore_heap_allocate(sz)
#define kfree(ptr)  xscore_heap_free(ptr)

int lumen_render_init(struct lumen_render_context *ctx, struct xscore_drm_device *drm) {
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(*ctx));

    ctx->drm = drm;
    ctx->screen_width = 1024U;
    ctx->screen_height = 768U;
    ctx->screen_stride = 1024U * 4U;

    /* Allocate backbuffer for compositing */
    size_t bb_size = (size_t)ctx->screen_width * ctx->screen_height * 4U;
    ctx->backbuffer = (uint32_t *)kmalloc(bb_size);
    if (!ctx->backbuffer) {
        xscore_serial_write("[LUMEN] Failed to allocate compositor backbuffer\n");
        return -1;
    }
    memset(ctx->backbuffer, 0x1E, bb_size);

    ctx->needs_repaint = 1U;
    xscore_serial_write("[LUMEN] Hardware-accelerated composition engine initialized\n");
    return 0;
}

void lumen_render_destroy(struct lumen_render_context *ctx) {
    if (!ctx) return;
    if (ctx->backbuffer) {
        kfree(ctx->backbuffer);
        ctx->backbuffer = NULL;
    }
}

void lumen_render_damage_surface(struct lumen_render_context *ctx, struct lumen_surface *surface, const struct lumen_damage_rect *rect) {
    if (!ctx || !surface) return;
    if (rect) {
        surface->damage = *rect;
    } else {
        surface->damage.x = 0;
        surface->damage.y = 0;
        surface->damage.width = surface->width;
        surface->damage.height = surface->height;
    }
    ctx->needs_repaint = 1U;
}

int lumen_render_composite(struct lumen_render_context *ctx, struct lumen_surface *surfaces_head) {
    if (!ctx || !ctx->backbuffer) return -1;

    /* Clean solid background */
    uint32_t bg_color = 0xFF1A1A24; /* Dark slate */
    size_t pixel_count = (size_t)ctx->screen_width * ctx->screen_height;
    for (size_t i = 0; i < pixel_count; ++i) {
        ctx->backbuffer[i] = bg_color;
    }

    /* Blend mapped surfaces bottom-to-top */
    struct lumen_surface *s = surfaces_head;
    while (s) {
        if (s->is_mapped && s->current_buffer && s->current_buffer->shm_data) {
            const uint32_t *src_pixels = (const uint32_t *)s->current_buffer->shm_data;
            int32_t sw = s->current_buffer->width;
            int32_t sh = s->current_buffer->height;

            for (int32_t sy = 0; sy < sh; ++sy) {
                int32_t dy = s->y + sy;
                if (dy < 0 || (uint32_t)dy >= ctx->screen_height) continue;

                for (int32_t sx = 0; sx < sw; ++sx) {
                    int32_t dx = s->x + sx;
                    if (dx < 0 || (uint32_t)dx >= ctx->screen_width) continue;

                    uint32_t src = src_pixels[sy * sw + sx];
                    uint32_t alpha = (src >> 24) & 0xFF;

                    if (alpha == 0xFF) {
                        ctx->backbuffer[dy * ctx->screen_width + dx] = src;
                    } else if (alpha > 0) {
                        uint32_t dst = ctx->backbuffer[dy * ctx->screen_width + dx];
                        uint32_t inv_a = 255U - alpha;
                        uint32_t r = (((src >> 16) & 0xFF) * alpha + ((dst >> 16) & 0xFF) * inv_a) / 255U;
                        uint32_t g = (((src >> 8) & 0xFF) * alpha + ((dst >> 8) & 0xFF) * inv_a) / 255U;
                        uint32_t b = ((src & 0xFF) * alpha + (dst & 0xFF) * inv_a) / 255U;
                        ctx->backbuffer[dy * ctx->screen_width + dx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
        s = s->next;
    }

    ctx->needs_repaint = 0U;
    return 0;
}

int lumen_render_page_flip(struct lumen_render_context *ctx) {
    if (!ctx) return -1;
    ctx->vblank_seq++;

    /* If DRM device is registered, invoke atomic commit */
    if (ctx->drm) {
        struct drm_atomic_state state;
        memset(&state, 0, sizeof(state));
        state.flags = 0x01; /* Page flip commit */
        xscore_drm_atomic_commit(ctx->drm, &state);
    }

    return 0;
}
