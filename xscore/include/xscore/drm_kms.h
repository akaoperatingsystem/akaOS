
#ifndef XSCORE_DRM_KMS_H
#define XSCORE_DRM_KMS_H

#include <stdint.h>

/* DRM Object Types */
typedef uint32_t drm_mode_object_id;

struct drm_crtc {
    drm_mode_object_id id;
    uint32_t width;
    uint32_t height;
};

struct drm_connector {
    drm_mode_object_id id;
    int connected;
    uint32_t current_mode_id;
};

struct drm_plane {
    drm_mode_object_id id;
    uint32_t zpos;
};

/* GEM Object */
struct drm_gem_object {
    uint32_t handle;
    uint64_t size;
    uintptr_t vaddr;
    uint64_t paddr;
};

/* DRM Device Structure */
struct xscore_drm_device {
    struct xscore_gem_manager *gem_manager;
    struct xscore_gpu_device *gpu;
    struct drm_crtc *crtc;
    struct drm_connector *connector;
    struct drm_plane *primary_plane;
};

int xscore_drm_initialize(struct xscore_gpu_device *gpu);


/* GEM Object Flags */
#define DRM_GEM_WRITE 0x01
#define DRM_GEM_READ  0x02

struct drm_gem_object {
    uint32_t handle;
    uint64_t size;
    uintptr_t vaddr;
    uint64_t paddr;
    uint32_t flags;
    struct drm_gem_object *next;
};

/* GEM Manager */
struct xscore_gem_manager {
    struct drm_gem_object *objects;
    uint32_t next_handle;
};

int xscore_gem_init(struct xscore_drm_device *drm);
struct drm_gem_object* xscore_gem_create(struct xscore_drm_device *drm, uint64_t size, uint32_t flags);
int xscore_gem_destroy(struct xscore_drm_device *drm, uint32_t handle);
int xscore_gem_map(struct drm_gem_object *obj, uintptr_t vaddr);


/* DRM Atomic Mode Setting Structures */
struct drm_crtc_state {
    uint32_t mode_id;
    uint32_t active;
    uint32_t framebuffer_id;
};

struct drm_connector_state {
    uint32_t connector_id;
    uint32_t crtc_id;
    uint32_t status; /* Connected, Disconnected */
};

struct drm_atomic_state {
    struct drm_crtc_state crtc_state;
    struct drm_connector_state connector_state;
};

/* Atomic Commit API */
int xscore_drm_atomic_commit(struct xscore_drm_device *drm, struct drm_atomic_state *state);


/* I2C/DDC Interface for EDID Discovery */
struct drm_i2c_adapter {
    void (*send_byte)(uint8_t byte);
    uint8_t (*receive_byte)(void);
    void (*start)(void);
    void (*stop)(void);
};

/* EDID data structure */
struct drm_edid {
    uint8_t data[128];
};

int xscore_drm_get_edid(struct xscore_drm_device *drm, struct drm_edid *edid);

#endif /* XSCORE_DRM_KMS_H */
