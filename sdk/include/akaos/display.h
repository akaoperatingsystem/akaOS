#ifndef AKAOS_DISPLAY_H
#define AKAOS_DISPLAY_H

#include <stdint.h>

#define AKAOS_FRAMEBUFFER_PATH "/dev/fb0"
#define AKAOS_DISPLAY_ABI_VERSION UINT32_C(2)

#define AKAOS_DISPLAY_PIXEL_RGBX8888 UINT32_C(0)
#define AKAOS_DISPLAY_PIXEL_BGRX8888 UINT32_C(1)
#define AKAOS_SURFACE_FORMAT_XRGB8888 UINT32_C(1)
#define AKAOS_SURFACE_FORMAT_ARGB8888 UINT32_C(2)

#define AKAOS_DISPLAY_MSG_ATTACH UINT32_C(1)
#define AKAOS_DISPLAY_MSG_DAMAGE UINT32_C(2)
#define AKAOS_DISPLAY_MSG_DESTROY UINT32_C(3)
#define AKAOS_DISPLAY_MSG_FOCUS UINT32_C(4)
#define AKAOS_DISPLAY_MSG_MOVE UINT32_C(5)
#define AKAOS_DISPLAY_MSG_RESIZE UINT32_C(6)
#define AKAOS_DISPLAY_MSG_CONFIGURE_ACK UINT32_C(7)
#define AKAOS_DISPLAY_MSG_STACK UINT32_C(8)
#define AKAOS_DISPLAY_MSG_STATE UINT32_C(9)

struct akaos_display_info {
    uint32_t abi_version;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t pixel_format;
    uint32_t reserved;
    uint64_t byte_size;
};

struct akaos_surface_header {
    uint32_t abi_version;
    uint32_t width;
    uint32_t height;
    uint32_t stride_pixels;
    uint32_t format;
    uint32_t pixel_offset;
    uint64_t byte_size;
};

struct akaos_display_message {
    uint32_t type;
    uint32_t flags;
    uint32_t surface_id;
    uint32_t client_pid;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t z_order;
    uint32_t damage_x;
    uint32_t damage_y;
    uint32_t damage_w;
    uint32_t damage_h;
    char shm_name[32];
    char title[32];
    char icon_name[20];
    /* Client-generated configure serial used to acknowledge a resize. */
    uint32_t configure_serial;
    uint32_t parent_surface_id;
};

#define AKAOS_COMPOSITOR_IPC_PATH "/lumen-ipc"
#define AKAOS_COMPOSITOR_MAX_SURFACES 8U
#define AKAOS_COMPOSITOR_MAX_MESSAGES 32U
#define AKAOS_COMPOSITOR_PROTOCOL_VERSION UINT32_C(9)

#define AKAOS_SURFACE_FLAG_WINDOW    UINT32_C(0x00)
#define AKAOS_SURFACE_FLAG_DESKTOP   UINT32_C(0x01)
#define AKAOS_SURFACE_FLAG_PANEL     UINT32_C(0x02)
#define AKAOS_SURFACE_FLAG_MINIMIZED UINT32_C(0x04)
#define AKAOS_SURFACE_FLAG_WAYLAND   UINT32_C(0x08)
#define AKAOS_SURFACE_FLAG_POPUP     UINT32_C(0x10)
#define AKAOS_SURFACE_FLAG_CURSOR    UINT32_C(0x20)
#define AKAOS_SURFACE_FLAG_SUBSURFACE UINT32_C(0x40)
#define AKAOS_SURFACE_FLAG_SUBSURFACE_ABOVE UINT32_C(0x80)
#define AKAOS_SURFACE_FLAG_MAXIMIZED UINT32_C(0x100)
#define AKAOS_SURFACE_FLAG_FULLSCREEN UINT32_C(0x200)
#define AKAOS_SURFACE_FLAG_LAYER_SHELL UINT32_C(0x400)
#define AKAOS_SURFACE_LAYER_SHIFT 11U
#define AKAOS_SURFACE_LAYER_MASK UINT32_C(0x1800)

#define AKAOS_SHELL_EVENT_POINTER UINT32_C(1)
#define AKAOS_SHELL_EVENT_BUTTON  UINT32_C(2)
#define AKAOS_SHELL_EVENT_KEY     UINT32_C(3)
#define AKAOS_SHELL_EVENT_TEXT    UINT32_C(4)
#define AKAOS_SHELL_EVENT_CAPACITY 32U
#define AKAOS_WAYLAND_INPUT_CAPACITY 64U
#define AKAOS_SHELL_FLAG_LAUNCHER_OPEN UINT32_C(0x01)
#define AKAOS_NOTIFICATION_CAPACITY 8U

struct akaos_shell_event {
    uint32_t type;
    uint32_t code;
    int32_t value;
    int32_t pointer_x;
    int32_t pointer_y;
};

struct akaos_wayland_input_event {
    uint32_t type;
    uint32_t code;
    int32_t value;
    int32_t pointer_x;
    int32_t pointer_y;
};

struct akaos_notification {
    uint32_t timeout_ms;
    uint32_t flags;
    char app_id[20];
    char title[32];
    char body[96];
};

struct akaos_compositor_slot {
    uint32_t active;
    uint32_t surface_id;
    uint32_t client_pid;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t z_order;
    uint32_t damage_seq;
    uint32_t damage_x;
    uint32_t damage_y;
    uint32_t damage_w;
    uint32_t damage_h;
    uint32_t flags;
    char shm_name[32];
    char title[32];
    char icon_name[20];
    /* The compositor publishes the latest resize serial and the serial the
     * client has explicitly acknowledged. */
    uint32_t configure_serial;
    uint32_t configure_ack;
    uint32_t parent_surface_id;
};

struct akaos_compositor_channel {
    uint32_t abi_version;
    uint32_t compositor_pid;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t surface_count;
    uint32_t active_surface_id;
    struct akaos_compositor_slot surfaces[AKAOS_COMPOSITOR_MAX_SURFACES];
    /* Bounded producer/consumer queue for clients.  The compositor is the
     * sole owner of the surface table; clients submit lifecycle and damage
     * messages here rather than racing it with direct writes. */
    uint32_t protocol_version;
    volatile uint32_t request_lock;
    volatile uint32_t request_head;
    volatile uint32_t request_tail;
    struct akaos_display_message requests[AKAOS_COMPOSITOR_MAX_MESSAGES];
    /* Compositor-to-shell input queue. Lumen is the sole producer and the
     * resident desktop shell is the sole consumer. */
    volatile uint32_t shell_event_lock;
    volatile uint32_t shell_event_head;
    volatile uint32_t shell_event_tail;
    volatile uint32_t shell_flags;
    volatile uint32_t shell_launcher_x;
    volatile uint32_t shell_task_count;
    struct akaos_shell_event shell_events[AKAOS_SHELL_EVENT_CAPACITY];
    volatile uint32_t notification_lock;
    volatile uint32_t notification_head;
    volatile uint32_t notification_tail;
    struct akaos_notification notifications[AKAOS_NOTIFICATION_CAPACITY];
    /* Lumen is the sole producer; the Wayland bridge is the sole consumer. */
    volatile uint32_t wayland_input_lock;
    volatile uint32_t wayland_input_head;
    volatile uint32_t wayland_input_tail;
    struct akaos_wayland_input_event wayland_input[AKAOS_WAYLAND_INPUT_CAPACITY];
};

/* Shared-memory channel helpers.  These are header-only so every supported
 * userspace architecture gets the same acquire/release protocol without a
 * libc dependency.  Callers should hold no other akaOS locks while waiting. */
static inline void akaos_compositor_lock(struct akaos_compositor_channel *channel) {
    if (!channel) return;
    while (__atomic_exchange_n(&channel->request_lock, 1U, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&channel->request_lock, __ATOMIC_RELAXED)) {
#if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
            __asm__ volatile("yield" ::: "memory");
#elif defined(__riscv)
            /* Keep the queue portable to RV32/RV64 cores without Zihintpause. */
            __asm__ volatile("nop" ::: "memory");
#endif
        }
    }
}

static inline void akaos_compositor_unlock(struct akaos_compositor_channel *channel) {
    if (!channel) return;
    __atomic_store_n(&channel->request_lock, 0U, __ATOMIC_RELEASE);
}

static inline int akaos_compositor_submit(
    struct akaos_compositor_channel *channel,
    const struct akaos_display_message *message) {
    if (!channel || !message || channel->abi_version != AKAOS_DISPLAY_ABI_VERSION ||
        channel->protocol_version != AKAOS_COMPOSITOR_PROTOCOL_VERSION) return 0;

    akaos_compositor_lock(channel);
    uint32_t head = __atomic_load_n(&channel->request_head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&channel->request_tail, __ATOMIC_ACQUIRE);
    uint32_t next = (head + 1U) % AKAOS_COMPOSITOR_MAX_MESSAGES;
    if (next == tail) {
        akaos_compositor_unlock(channel);
        return 0;
    }
    channel->requests[head] = *message;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&channel->request_head, next, __ATOMIC_RELEASE);
    akaos_compositor_unlock(channel);
    return 1;
}

static inline int akaos_compositor_receive(
    struct akaos_compositor_channel *channel,
    struct akaos_display_message *message) {
    if (!channel || !message) return 0;
    akaos_compositor_lock(channel);
    uint32_t tail = __atomic_load_n(&channel->request_tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&channel->request_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        akaos_compositor_unlock(channel);
        return 0;
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    *message = channel->requests[tail];
    __atomic_store_n(&channel->request_tail,
                     (tail + 1U) % AKAOS_COMPOSITOR_MAX_MESSAGES,
                     __ATOMIC_RELEASE);
    akaos_compositor_unlock(channel);
    return 1;
}

static inline int akaos_wayland_input_submit(
    struct akaos_compositor_channel *channel,
    const struct akaos_wayland_input_event *event) {
    if (!channel || !event ||
        channel->abi_version != AKAOS_DISPLAY_ABI_VERSION ||
        channel->protocol_version != AKAOS_COMPOSITOR_PROTOCOL_VERSION)
        return 0;
    while (__atomic_exchange_n(&channel->wayland_input_lock, 1U,
                               __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&channel->wayland_input_lock,
                               __ATOMIC_RELAXED)) {
#if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
            __asm__ volatile("yield" ::: "memory");
#elif defined(__riscv)
            __asm__ volatile("nop" ::: "memory");
#endif
        }
    }
    uint32_t head = __atomic_load_n(&channel->wayland_input_head,
                                    __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&channel->wayland_input_tail,
                                    __ATOMIC_ACQUIRE);
    uint32_t next = (head + 1U) % AKAOS_WAYLAND_INPUT_CAPACITY;
    if (next == tail) {
        __atomic_store_n(&channel->wayland_input_lock, 0U, __ATOMIC_RELEASE);
        return 0;
    }
    channel->wayland_input[head] = *event;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&channel->wayland_input_head, next, __ATOMIC_RELEASE);
    __atomic_store_n(&channel->wayland_input_lock, 0U, __ATOMIC_RELEASE);
    return 1;
}

static inline int akaos_wayland_input_receive(
    struct akaos_compositor_channel *channel,
    struct akaos_wayland_input_event *event) {
    if (!channel || !event) return 0;
    while (__atomic_exchange_n(&channel->wayland_input_lock, 1U,
                               __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&channel->wayland_input_lock,
                               __ATOMIC_RELAXED)) {
#if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
            __asm__ volatile("yield" ::: "memory");
#elif defined(__riscv)
            __asm__ volatile("nop" ::: "memory");
#endif
        }
    }
    uint32_t tail = __atomic_load_n(&channel->wayland_input_tail,
                                    __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&channel->wayland_input_head,
                                    __ATOMIC_ACQUIRE);
    if (tail == head) {
        __atomic_store_n(&channel->wayland_input_lock, 0U, __ATOMIC_RELEASE);
        return 0;
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    *event = channel->wayland_input[tail];
    __atomic_store_n(&channel->wayland_input_tail,
                     (tail + 1U) % AKAOS_WAYLAND_INPUT_CAPACITY,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&channel->wayland_input_lock, 0U, __ATOMIC_RELEASE);
    return 1;
}

static inline void akaos_compositor_damage(struct akaos_compositor_slot *slot,
                                           uint32_t x, uint32_t y,
                                           uint32_t width, uint32_t height) {
    if (!slot || width == 0 || height == 0) return;
    slot->damage_x = x;
    slot->damage_y = y;
    slot->damage_w = width;
    slot->damage_h = height;
    __atomic_add_fetch(&slot->damage_seq, 1U, __ATOMIC_RELEASE);
}

static inline void akaos_shell_event_lock(struct akaos_compositor_channel *channel) {
    if (!channel) return;
    while (__atomic_exchange_n(&channel->shell_event_lock, 1U, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&channel->shell_event_lock, __ATOMIC_RELAXED)) {
#if defined(__x86_64__) || defined(__i386__)
            __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
            __asm__ volatile("yield" ::: "memory");
#elif defined(__riscv)
            __asm__ volatile("nop" ::: "memory");
#endif
        }
    }
}

static inline void akaos_shell_event_unlock(struct akaos_compositor_channel *channel) {
    if (channel) __atomic_store_n(&channel->shell_event_lock, 0U, __ATOMIC_RELEASE);
}

static inline int akaos_shell_event_submit(struct akaos_compositor_channel *channel,
                                           const struct akaos_shell_event *event) {
    if (!channel || !event) return 0;
    akaos_shell_event_lock(channel);
    uint32_t head = __atomic_load_n(&channel->shell_event_head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&channel->shell_event_tail, __ATOMIC_ACQUIRE);
    uint32_t next = (head + 1U) % AKAOS_SHELL_EVENT_CAPACITY;
    if (next == tail) {
        akaos_shell_event_unlock(channel);
        return 0;
    }
    channel->shell_events[head] = *event;
    __atomic_store_n(&channel->shell_event_head, next, __ATOMIC_RELEASE);
    akaos_shell_event_unlock(channel);
    return 1;
}

static inline int akaos_shell_event_receive(struct akaos_compositor_channel *channel,
                                            struct akaos_shell_event *event) {
    if (!channel || !event) return 0;
    akaos_shell_event_lock(channel);
    uint32_t tail = __atomic_load_n(&channel->shell_event_tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&channel->shell_event_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        akaos_shell_event_unlock(channel);
        return 0;
    }
    *event = channel->shell_events[tail];
    __atomic_store_n(&channel->shell_event_tail,
                     (tail + 1U) % AKAOS_SHELL_EVENT_CAPACITY,
                     __ATOMIC_RELEASE);
    akaos_shell_event_unlock(channel);
    return 1;
}

static inline int akaos_notification_submit(
    struct akaos_compositor_channel *channel,
    const struct akaos_notification *notification) {
    if (!channel || !notification) return 0;
    while (__atomic_exchange_n(&channel->notification_lock, 1U, __ATOMIC_ACQUIRE)) { }
    uint32_t head = __atomic_load_n(&channel->notification_head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&channel->notification_tail, __ATOMIC_ACQUIRE);
    uint32_t next = (head + 1U) % AKAOS_NOTIFICATION_CAPACITY;
    if (next == tail) {
        __atomic_store_n(&channel->notification_lock, 0U, __ATOMIC_RELEASE);
        return 0;
    }
    channel->notifications[head] = *notification;
    __atomic_store_n(&channel->notification_head, next, __ATOMIC_RELEASE);
    __atomic_store_n(&channel->notification_lock, 0U, __ATOMIC_RELEASE);
    return 1;
}

static inline int akaos_notification_receive(
    struct akaos_compositor_channel *channel,
    struct akaos_notification *notification) {
    if (!channel || !notification) return 0;
    while (__atomic_exchange_n(&channel->notification_lock, 1U, __ATOMIC_ACQUIRE)) { }
    uint32_t tail = __atomic_load_n(&channel->notification_tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&channel->notification_head, __ATOMIC_ACQUIRE);
    if (tail == head) {
        __atomic_store_n(&channel->notification_lock, 0U, __ATOMIC_RELEASE);
        return 0;
    }
    *notification = channel->notifications[tail];
    __atomic_store_n(&channel->notification_tail,
                     (tail + 1U) % AKAOS_NOTIFICATION_CAPACITY,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&channel->notification_lock, 0U, __ATOMIC_RELEASE);
    return 1;
}

#endif
