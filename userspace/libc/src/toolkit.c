#include <akaos/display.h>
#include <akaos/toolkit.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>

static void toolkit_copy_string(char *destination, size_t capacity,
                                const char *source) {
    if (!destination || capacity == 0) return;
    if (!source) source = "";
    size_t index = 0;
    while (index + 1U < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

int akaos_toolkit_connect(struct akaos_compositor_channel **channel_out) {
    if (!channel_out) return -1;
    *channel_out = 0;
    int fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0);
    if (fd < 0) return -2;
    void *mapping = mmap(NULL, sizeof(struct akaos_compositor_channel),
                         PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) return -3;
    struct akaos_compositor_channel *channel = mapping;
    if (channel->abi_version != AKAOS_DISPLAY_ABI_VERSION ||
        channel->protocol_version != AKAOS_COMPOSITOR_PROTOCOL_VERSION) {
        (void)munmap(mapping, sizeof(*channel));
        return -4;
    }
    *channel_out = channel;
    return 0;
}

int akaos_toolkit_display_info(struct akaos_display_info *info) {
    if (!info) return -1;
    struct akaos_compositor_channel *channel = 0;
    if (akaos_toolkit_connect(&channel) != 0) return -1;
    info->abi_version = channel->abi_version;
    info->width = channel->display_width;
    info->height = channel->display_height;
    info->stride_pixels = channel->display_width;
    info->pixel_format = AKAOS_DISPLAY_PIXEL_RGBX8888;
    info->reserved = 0;
    info->byte_size = (uint64_t)info->width * info->height * sizeof(uint32_t);
    (void)munmap(channel, sizeof(*channel));
    return 0;
}

int akaos_toolkit_disconnect(struct akaos_compositor_channel *channel) {
    if (!channel) return -1;
    return munmap(channel, sizeof(*channel));
}

static int toolkit_surface_message(const struct akaos_toolkit_surface *surface,
                                   uint32_t type, int32_t x, int32_t y,
                                   uint32_t width, uint32_t height,
                                   uint32_t damage_x, uint32_t damage_y,
                                   uint32_t damage_w, uint32_t damage_h) {
    if (!surface || !surface->channel || surface->surface_id == 0 ||
        surface->client_pid == 0) return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = type;
    message.surface_id = surface->surface_id;
    message.client_pid = surface->client_pid;
    message.x = x;
    message.y = y;
    message.width = width;
    message.height = height;
    message.damage_x = damage_x;
    message.damage_y = damage_y;
    message.damage_w = damage_w;
    message.damage_h = damage_h;
    return akaos_compositor_submit(surface->channel, &message);
}

int akaos_toolkit_surface_attach(struct akaos_toolkit_surface *surface,
                                 struct akaos_compositor_channel *channel,
                                 uint32_t surface_id, int32_t x, int32_t y,
                                 uint32_t width, uint32_t height,
                                 const char *shm_name, const char *title,
                                 const char *icon_name) {
    if (!surface || !channel || surface_id == 0 || width == 0 || height == 0 ||
        !shm_name || shm_name[0] == '\0') return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_ATTACH;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    message.x = x;
    message.y = y;
    message.width = width;
    message.height = height;
    message.z_order = 1;
    toolkit_copy_string(message.shm_name, sizeof(message.shm_name), shm_name);
    toolkit_copy_string(message.title, sizeof(message.title), title);
    toolkit_copy_string(message.icon_name, sizeof(message.icon_name), icon_name);
    if (!akaos_compositor_submit(channel, &message)) return 0;
    surface->channel = channel;
    surface->surface_id = surface_id;
    surface->client_pid = message.client_pid;
    surface->next_configure_serial = 0;
    return 1;
}

int akaos_toolkit_surface_damage(const struct akaos_toolkit_surface *surface,
                                 uint32_t x, uint32_t y,
                                 uint32_t width, uint32_t height) {
    return toolkit_surface_message(surface, AKAOS_DISPLAY_MSG_DAMAGE,
                                   0, 0, 0, 0, x, y, width, height);
}

int akaos_toolkit_surface_move(const struct akaos_toolkit_surface *surface,
                               int32_t x, int32_t y) {
    return toolkit_surface_message(surface, AKAOS_DISPLAY_MSG_MOVE,
                                   x, y, 0, 0, 0, 0, 0, 0);
}

int akaos_toolkit_surface_resize(struct akaos_toolkit_surface *surface,
                                 int32_t x, int32_t y,
                                 uint32_t width, uint32_t height,
                                 const char *shm_name) {
    if (!surface || !surface->channel || surface->surface_id == 0 ||
        surface->client_pid == 0 || width == 0 || height == 0 ||
        !shm_name || shm_name[0] == '\0') return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_RESIZE;
    message.surface_id = surface->surface_id;
    message.client_pid = surface->client_pid;
    message.x = x;
    message.y = y;
    message.width = width;
    message.height = height;
    message.damage_w = width;
    message.damage_h = height;
    uint32_t serial = surface->next_configure_serial + 1U;
    if (serial == 0) serial = 1U;
    surface->next_configure_serial = serial;
    message.configure_serial = serial;
    toolkit_copy_string(message.shm_name, sizeof(message.shm_name), shm_name);
    return akaos_compositor_submit(surface->channel, &message);
}

uint32_t akaos_toolkit_surface_configure_serial(
    const struct akaos_toolkit_surface *surface) {
    return surface ? surface->next_configure_serial : 0U;
}

static const struct akaos_compositor_slot *toolkit_surface_slot(
    const struct akaos_toolkit_surface *surface) {
    if (!surface || !surface->channel || surface->surface_id == 0 ||
        surface->client_pid == 0) return 0;
    for (size_t index = 0; index < AKAOS_COMPOSITOR_MAX_SURFACES; ++index) {
        const struct akaos_compositor_slot *slot =
            &surface->channel->surfaces[index];
        if (slot->active && slot->surface_id == surface->surface_id &&
            slot->client_pid == surface->client_pid) return slot;
    }
    return 0;
}

int akaos_toolkit_surface_wait_configure(
    const struct akaos_toolkit_surface *surface, uint32_t serial,
    uint32_t timeout_ms) {
    if (!surface || serial == 0) return 0;
    uint32_t elapsed = 0;
    for (;;) {
        const struct akaos_compositor_slot *slot = toolkit_surface_slot(surface);
        if (slot && slot->configure_serial == serial &&
            slot->width != 0 && slot->height != 0) return 1;
        if (elapsed >= timeout_ms) return 0;
        uint32_t remaining = timeout_ms - elapsed;
        int slice = remaining > 10U ? 10 : (int)remaining;
        if (slice <= 0) slice = 1;
        (void)poll(0, 0, slice);
        elapsed += (uint32_t)slice;
    }
}

int akaos_toolkit_surface_ack_configure(
    const struct akaos_toolkit_surface *surface, uint32_t serial) {
    if (!surface || !surface->channel || surface->surface_id == 0 ||
        surface->client_pid == 0 || serial == 0) return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_CONFIGURE_ACK;
    message.surface_id = surface->surface_id;
    message.client_pid = surface->client_pid;
    message.configure_serial = serial;
    return akaos_compositor_submit(surface->channel, &message);
}

int akaos_toolkit_surface_wait_configure_ack(
    const struct akaos_toolkit_surface *surface, uint32_t serial,
    uint32_t timeout_ms) {
    if (!surface || serial == 0) return 0;
    uint32_t elapsed = 0;
    for (;;) {
        const struct akaos_compositor_slot *slot = toolkit_surface_slot(surface);
        if (slot && slot->configure_serial == serial &&
            slot->configure_ack == serial) return 1;
        if (elapsed >= timeout_ms) return 0;
        uint32_t remaining = timeout_ms - elapsed;
        int slice = remaining > 10U ? 10 : (int)remaining;
        if (slice <= 0) slice = 1;
        (void)poll(0, 0, slice);
        elapsed += (uint32_t)slice;
    }
}

int akaos_toolkit_surface_focus(const struct akaos_toolkit_surface *surface) {
    return toolkit_surface_message(surface, AKAOS_DISPLAY_MSG_FOCUS,
                                   0, 0, 0, 0, 0, 0, 0, 0);
}

int akaos_toolkit_surface_destroy(const struct akaos_toolkit_surface *surface) {
    return toolkit_surface_message(surface, AKAOS_DISPLAY_MSG_DESTROY,
                                   0, 0, 0, 0, 0, 0, 0, 0);
}
