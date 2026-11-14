
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
    uint32_t crtc_id;
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


/* Command Submission & Ring Buffer */
#define DRM_RING_SIZE 4096

struct drm_fence {
    uint32_t seqno;
    int signaled;
};

struct drm_ring_buffer {
    uint32_t *commands;
    uint32_t head;
    uint32_t tail;
    uint32_t size;
};

int xscore_drm_ring_init(struct xscore_drm_device *drm);
int xscore_drm_submit_command(struct xscore_drm_device *drm, uint32_t *cmds, size_t count, struct drm_fence *fence);
int xscore_drm_fence_wait(struct drm_fence *fence);


/* Firmware Loading Interface */
struct drm_firmware_header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t checksum;
};

int xscore_drm_load_firmware(struct xscore_drm_device *drm, const char *fw_path);


/* Interrupt Handlers */
void xscore_drm_handle_vblank(struct xscore_drm_device *drm);
void xscore_drm_handle_command_completion(struct xscore_drm_device *drm, uint32_t seqno);
void xscore_drm_handle_error(struct xscore_drm_device *drm, uint32_t error_code);


/* DRM/KMS IOCTLs */
#define DRM_IOCTL_BASE 'd'
#define DRM_IOCTL_VERSION 0x00
#define DRM_IOCTL_GEM_CREATE 0x01
#define DRM_IOCTL_GEM_MAP 0x02
#define DRM_IOCTL_MODE_ATOMIC_COMMIT 0x03

struct drm_ioctl_gem_create {
    uint64_t size;
    uint32_t handle;
    uint32_t flags;
};

struct drm_ioctl_gem_map {
    uint32_t handle;
    uintptr_t vaddr;
};

void xscore_drm_register_irq_handler(void (*vblank)(struct xscore_drm_device *));
int xscore_drm_map_bar(struct xscore_drm_device *drm, int bar_index);
int xscore_drm_accel_fill_rect(struct xscore_drm_device *drm, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
int xscore_drm_virtio_submit_cmd(struct xscore_drm_device *drm, uint32_t *cmd, size_t size);
int xscore_drm_register_device(struct xscore_drm_device *drm);
#endif /* XSCORE_DRM_KMS_H */
