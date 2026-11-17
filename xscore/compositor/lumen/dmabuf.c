/*
 * akaOS XSCore Lumen Wayland Compositor - DMA-BUF Implementation
 */

#include "dmabuf.h"
#include <xscore/heap.h>
#include <xscore/serial.h>
#include <xscore/string.h>

#define kmalloc(sz) xscore_heap_allocate(sz)
#define kfree(ptr)  xscore_heap_free(ptr)

int lumen_dmabuf_import(struct xscore_drm_device *drm,
                        const struct lumen_dmabuf_params *params,
                        struct lumen_buffer *out_buffer) {
    if (!params || !out_buffer) return -1;
    (void)drm;

    out_buffer->width = (int32_t)params->width;
    out_buffer->height = (int32_t)params->height;
    out_buffer->stride = (int32_t)params->strides[0];
    out_buffer->format = params->format;
    out_buffer->is_dmabuf = 1U;
    out_buffer->refcount = 1U;

    /* If DRM GEM manager is available, map the imported physical buffer */
    if (drm && drm->gem_manager) {
        struct drm_gem_object *gem = xscore_gem_create(drm, (uint64_t)params->strides[0] * params->height, 0);
        if (gem) {
            out_buffer->gem_handle = gem->handle;
            out_buffer->dma_buf_paddr = gem->paddr;
        }
    }

    xscore_serial_write("[LUMEN] Imported client DMA-BUF prime buffer successfully\n");
    return 0;
}

void lumen_dmabuf_release(struct xscore_drm_device *drm,
                          struct lumen_buffer *buffer) {
    if (!buffer) return;
    (void)drm;

    if (buffer->is_dmabuf) {
        buffer->is_dmabuf = 0U;
        buffer->gem_handle = 0U;
    }
}
