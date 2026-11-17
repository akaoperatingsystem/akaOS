/*
 * akaOS XSCore Lumen Wayland Compositor - Subsystem Header
 */

#ifndef XSCORE_COMPOSITOR_LUMEN_H
#define XSCORE_COMPOSITOR_LUMEN_H

#include "protocol.h"
#include "render.h"
#include "dmabuf.h"
#include "input.h"

struct lumen_compositor {
    struct lumen_render_context render;
    struct lumen_input_seat seat;
    struct lumen_surface *surfaces_head;
    uint32_t active_surface_count;
    uint32_t running;
};

int lumen_compositor_init(struct lumen_compositor *lumen, struct xscore_drm_device *drm);
void lumen_compositor_destroy(struct lumen_compositor *lumen);
int lumen_compositor_dispatch_frame(struct lumen_compositor *lumen);

#endif /* XSCORE_COMPOSITOR_LUMEN_H */
