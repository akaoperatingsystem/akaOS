/*
 * akaOS XSCore Lumen Wayland Compositor - Main Implementation
 */

#include "lumen.h"
#include <xscore/serial.h>
#include <xscore/string.h>

int lumen_compositor_init(struct lumen_compositor *lumen, struct xscore_drm_device *drm) {
    if (!lumen) return -1;
    memset(lumen, 0, sizeof(*lumen));

    lumen_protocol_init();
    if (lumen_render_init(&lumen->render, drm) != 0) {
        return -1;
    }
    lumen_input_init(&lumen->seat, drm);

    lumen->running = 1U;
    xscore_serial_write("[LUMEN] Native Wayland Compositor fully operational\n");
    return 0;
}

void lumen_compositor_destroy(struct lumen_compositor *lumen) {
    if (!lumen) return;
    lumen->running = 0U;
    lumen_input_destroy(&lumen->seat);
    lumen_render_destroy(&lumen->render);
    lumen_protocol_destroy();
}

int lumen_compositor_dispatch_frame(struct lumen_compositor *lumen) {
    if (!lumen || !lumen->running) return -1;

    lumen_render_composite(&lumen->render, lumen->surfaces_head);
    lumen_render_page_flip(&lumen->render);
    return 0;
}
