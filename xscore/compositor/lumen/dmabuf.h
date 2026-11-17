/*
 * akaOS XSCore Lumen Wayland Compositor - DMA-BUF & Hardware Buffer Management
 */

#ifndef LUMEN_WAYLAND_DMABUF_H
#define LUMEN_WAYLAND_DMABUF_H

#include "protocol.h"
#include <xscore/drm_kms.h>
#include <stdint.h>

struct lumen_dmabuf_params {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t flags;
    uint64_t modifier;
    int fds[4];
    uint32_t strides[4];
    uint32_t offsets[4];
    uint32_t plane_count;
};

int lumen_dmabuf_import(struct xscore_drm_device *drm,
                        const struct lumen_dmabuf_params *params,
                        struct lumen_buffer *out_buffer);
void lumen_dmabuf_release(struct xscore_drm_device *drm,
                          struct lumen_buffer *buffer);

#endif /* LUMEN_WAYLAND_DMABUF_H */
