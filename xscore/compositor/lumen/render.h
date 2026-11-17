/*
 * akaOS XSCore Lumen Wayland Compositor - Hardware Render Pipeline & DRM/KMS
 *
 * Provides atomic modesetting, surface compositing, damage-clipped scanout,
 * and DMA-BUF hardware buffer integration.
 */

#ifndef LUMEN_WAYLAND_RENDER_H
#define LUMEN_WAYLAND_RENDER_H

#include "protocol.h"
#include <xscore/drm_kms.h>
#include <stdint.h>

struct lumen_render_context {
    struct xscore_drm_device *drm;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t screen_stride;
    uint32_t *backbuffer;
    uint64_t backbuffer_paddr;
    uint32_t backbuffer_gem_handle;
    uint32_t vblank_seq;
    uint32_t needs_repaint;
};

int lumen_render_init(struct lumen_render_context *ctx, struct xscore_drm_device *drm);
void lumen_render_destroy(struct lumen_render_context *ctx);
void lumen_render_damage_surface(struct lumen_render_context *ctx, struct lumen_surface *surface, const struct lumen_damage_rect *rect);
int lumen_render_composite(struct lumen_render_context *ctx, struct lumen_surface *surfaces_head);
int lumen_render_page_flip(struct lumen_render_context *ctx);

#endif /* LUMEN_WAYLAND_RENDER_H */
