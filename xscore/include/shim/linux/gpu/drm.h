#ifndef _SHIM_LINUX_GPU_DRM_H
#define _SHIM_LINUX_GPU_DRM_H

#include <shim/linux/types.h>

// Minimal DRM shim structure
struct drm_device;
struct drm_driver;

int drm_dev_init(struct drm_device *dev, const struct drm_driver *driver, struct device *parent);
void drm_dev_put(struct drm_device *dev);

#endif /* _SHIM_LINUX_GPU_DRM_H */
