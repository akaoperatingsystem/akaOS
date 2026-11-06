/* akaOS X11 display server.
 *
 * The server owns a single screen and publishes it to the Lumen compositor
 * as one surface.  X11 clients draw into the screen buffer exactly as they
 * would on a plain X server without the Composite extension, and the server
 * repaints the screen from its per-window backing store whenever the window
 * tree changes.
 */
#include <akaos/display.h>
#include <akaos/input.h>
#include <akaos/toolkit.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "server.h"

static struct x_server server;
static int input_descriptor = -1;
static struct akaos_toolkit_surface root_surface;
static char root_shm_name[32];

/* The trace of the requests and replies is only written when it is asked for,
 * because the console of the guest is slow. */
int server_trace_enabled = 0;

static int create_root_surface(struct x_server *state) {
    size_t pixel_bytes = (size_t)state->width * (size_t)state->height *
                         sizeof(uint32_t);
    size_t mapping_size = sizeof(struct akaos_surface_header) + pixel_bytes;
    int name_length = snprintf(root_shm_name, sizeof(root_shm_name), "/xs-%u",
                               (uint32_t)getpid());
    if (name_length <= 0 || (size_t)name_length >= sizeof(root_shm_name)) return 0;

    int descriptor = shm_open(root_shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (descriptor < 0) return 0;
    if (ftruncate(descriptor, (off_t)mapping_size) != 0) {
        close(descriptor);
        shm_unlink(root_shm_name);
        return 0;
    }
    void *mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, descriptor, 0);
    int saved_errno = errno;
    close(descriptor);
    errno = saved_errno;
    if (mapping == MAP_FAILED) {
        shm_unlink(root_shm_name);
        return 0;
    }

    struct akaos_surface_header *header = mapping;
    memset(header, 0, sizeof(*header));
    header->abi_version = AKAOS_DISPLAY_ABI_VERSION;
    header->width = (uint32_t)state->width;
    header->height = (uint32_t)state->height;
    header->stride_pixels = (uint32_t)state->width;
    header->format = AKAOS_SURFACE_FORMAT_XRGB8888;
    header->pixel_offset = (uint32_t)sizeof(*header);
    header->byte_size = mapping_size;
    memset((uint8_t *)mapping + header->pixel_offset, 0, pixel_bytes);

    state->surface_pixels = (uint32_t *)((uint8_t *)mapping +
                                         header->pixel_offset);
    state->surface_bytes = pixel_bytes;
    state->screen = state->surface_pixels;
    return 1;
}

static int publish_root_surface(struct x_server *state) {
    if (!state->channel || !state->surface_pixels) return 0;
    memset(&root_surface, 0, sizeof(root_surface));
    root_surface.channel = state->channel;
    root_surface.surface_id = 1U;
    root_surface.client_pid = (uint32_t)getpid();
    if (!akaos_toolkit_surface_attach(&root_surface, state->channel, 1U, 0, 0,
                                      (uint32_t)state->width,
                                      (uint32_t)state->height,
                                      root_shm_name, "akaOS X11", "x11")) {
        return 0;
    }
    state->surface_id = 1;
    state->published = 1;
    return 1;
}

static int open_input_stream(void) {
    int descriptor = open(AKAOS_INPUT_EVENT_PATH, O_RDONLY);
    if (descriptor < 0) return -1;
    return descriptor;
}

void x_server_publish_damage(struct x_server *state) {
    if (!state->published || !state->channel) return;
    struct akaos_compositor_channel *channel = state->channel;

    /* Acquire the compositor request queue with a bounded spin.  The queue lock
     * is shared with the compositor, and a display server that waits on it
     * forever would stop answering its own clients. */
    uint32_t spins = 0U;
    while (__atomic_exchange_n(&channel->request_lock, 1U, __ATOMIC_ACQUIRE) != 0U) {
        if (++spins > 200000U) {
            printf("xserver: compositor queue is busy, skipping damage\n");
            fflush(stdout);
            return;
        }
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("pause");
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ volatile("yield");
#else
        __asm__ volatile("" ::: "memory");
#endif
    }

    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_DAMAGE;
    message.surface_id = root_surface.surface_id;
    message.client_pid = root_surface.client_pid;
    message.x = 0;
    message.y = 0;
    message.width = (uint32_t)state->width;
    message.height = (uint32_t)state->height;
    message.damage_x = 0;
    message.damage_y = 0;
    message.damage_w = (uint32_t)state->width;
    message.damage_h = (uint32_t)state->height;
    memcpy(message.shm_name, root_shm_name, sizeof(message.shm_name));

    uint32_t head = __atomic_load_n(&channel->request_head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&channel->request_tail, __ATOMIC_ACQUIRE);
    uint32_t next = (head + 1U) % AKAOS_COMPOSITOR_MAX_MESSAGES;
    if (next == tail) {
        /* The compositor has not consumed the previous damage request yet.
         * Coalesce instead of queueing another one: the compositor reads the
         * surface afresh on every frame, so a skipped request only costs a
         * frame of latency. */
        __atomic_store_n(&channel->request_lock, 0U, __ATOMIC_RELEASE);
        return;
    }
    channel->requests[head] = message;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&channel->request_head, next, __ATOMIC_RELEASE);
    __atomic_store_n(&channel->request_lock, 0U, __ATOMIC_RELEASE);
}

static int create_listen_socket(int display_number) {
    char directory_path[64];
    snprintf(directory_path, sizeof(directory_path), "/tmp/.X11-unix");
    (void)mkdir(directory_path, 0755);

    char socket_path[64];
    snprintf(socket_path, sizeof(socket_path), "/tmp/.X11-unix/X%d",
             display_number);
    (void)unlink(socket_path);

    int descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) return -1;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1U);
    if (bind(descriptor, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(descriptor);
        return -1;
    }
    if (listen(descriptor, 16) != 0) {
        close(descriptor);
        return -1;
    }
    return descriptor;
}

int main(int argc, char **argv) {
    int display_number = 0;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--display") == 0 && index + 1 < argc) {
            display_number = atoi(argv[++index]);
        } else if (strcmp(argv[index], "--trace") == 0) {
            server_trace_enabled = 1;
        } else if (strcmp(argv[index], "--check") == 0) {
            printf("xserver: display %d\n", display_number);
            return 0;
        } else if (strcmp(argv[index], "--version") == 0) {
            printf("xserver (akaOS X11 protocol 11.0)\n");
            return 0;
        } else if (strcmp(argv[index], "--help") == 0) {
            puts("Usage: xserver [--display N] [--trace]");
            return 0;
        }
    }

    memset(&server, 0, sizeof(server));
    server.width = X_SCREEN_WIDTH;
    server.height = X_SCREEN_HEIGHT;
    server.next_atom = 1U;
    server.focus_window = -1;
    server.pointer_window = -1;
    server.input_fd = -1;
    server.pointer_x = (int16_t)(server.width / 2);
    server.pointer_y = (int16_t)(server.height / 2);
    x_server_install_atoms(&server);

    server.listen_fd = create_listen_socket(display_number);
    if (server.listen_fd < 0) {
        fprintf(stderr, "xserver: cannot create the display socket: %s\n",
                strerror(errno));
        return 1;
    }

    int toolkit_rc = akaos_toolkit_connect(&server.channel);
    if (toolkit_rc != 0) {
        fprintf(stderr, "xserver: cannot reach the compositor channel (rc=%d, errno=%d)\n", toolkit_rc, errno);
        return 1;
    }
    /* Fault in the compositor request queue.
     *
     * The queue lives in the same shared file as the compositor state, and the
     * compositor reads the surface while it holds the queue lock.  Touching the
     * queue up front keeps the later submissions from faulting on a page
     * while the queue lock is held.  The queue is read only here: it belongs
     * to the compositor and other producers may already have queued work. */
    {
        volatile uint32_t queue_probe = 0U;
        for (size_t index = 0U; index < sizeof(server.channel->requests) /
                                       sizeof(server.channel->requests[0]);
             ++index) {
            queue_probe += server.channel->requests[index].type;
        }
        (void)queue_probe;
    }
    if (!create_root_surface(&server)) {
        fprintf(stderr, "xserver: cannot allocate the screen surface\n");
        return 1;
    }
    if (!publish_root_surface(&server)) {
        fprintf(stderr, "xserver: cannot publish the screen surface\n");
        fflush(stderr);
    } else {
        printf("xserver: screen surface published\n");
        fflush(stdout);
    }

    /* The root window owns the whole screen. */
    int root_index = x_window_create(&server, NULL, X_ROOT_WINDOW_ID, -1, 0, 0,
                                      (uint16_t)server.width,
                                      (uint16_t)server.height, 0,
                                      InputOutput, X_ROOT_VISUAL_ID);
    if (root_index < 0) {
        fprintf(stderr, "xserver: cannot create the root window\n");
        return 1;
    }
    server.windows[root_index].mapped = 1U;
    server.windows[root_index].background_pixel = 0x00202020U;
    server.windows[root_index].border_pixel = 0U;
    /* The root window keeps its own backing store so a client that draws
     * straight into the root still reaches the screen. */
    x_window_fill_backing(&server, root_index, 0x00202020U);
    /* The stacking order starts empty and holds the root window at the
     * bottom.  Every free slot has to read as empty, otherwise a newly
     * mapped window never reaches the stacking order. */
    for (int position = 0; position < X_MAX_WINDOWS; ++position) {
        server.stack[position] = -1;
    }
    server.stack[0] = root_index;
    server.window_count = 1;

    input_descriptor = open_input_stream();
    server.input_fd = input_descriptor;
    if (input_descriptor < 0) {
        printf("xserver: input device unavailable: %s\n", strerror(errno));
        fflush(stdout);
    }

    printf("xserver: ready on display %d (%dx%d)\n", display_number,
           server.width, server.height);
    fflush(stdout);

    unsigned long heartbeat = 0UL;
    for (;;) {
        struct pollfd descriptors[X_MAX_CLIENTS + 2];
        int count = 0;
        descriptors[count].fd = server.listen_fd;
        descriptors[count].events = POLLIN;
        descriptors[count].revents = 0;
        ++count;
        if (input_descriptor >= 0) {
            descriptors[count].fd = input_descriptor;
            descriptors[count].events = POLLIN;
            descriptors[count].revents = 0;
            ++count;
        }
        int client_slots[X_MAX_CLIENTS + 3];
        for (int index = 0; index < X_MAX_CLIENTS; ++index) {
            if (!server.clients[index].in_use) continue;
            descriptors[count].fd = server.clients[index].fd;
            descriptors[count].events = POLLIN;
            descriptors[count].revents = 0;
            client_slots[count] = index;
            ++count;
        }

        int ready = poll(descriptors, (nfds_t)count, 50);
        ++heartbeat;
        if (heartbeat % 1000UL == 0UL) {
            printf("xserver: loop alive, pointer %d,%d window 0x%x\n",
                   (int)server.pointer_x, (int)server.pointer_y,
                   server.pointer_window >= 0
                       ? (unsigned)server.windows[server.pointer_window].id
                       : 0U);
            fflush(stdout);
        }
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) {
            /* A quiet poll must not read the input device: the read blocks
             * until the next event arrives and the server would stop
             * accepting client connections. */
            continue;
        }

        int slot = 1;
        if (input_descriptor >= 0) {
            if ((descriptors[slot].revents & POLLIN) != 0) {
                x_input_pump(&server);
            }
            ++slot;
        }
        /* The descriptors of the clients follow the listen socket and the
         * input device, and every one of them names the client that owns
         * it.  Going through that list keeps the two in step even when a
         * client in the middle of the list goes away. */
        for (; slot < count; ++slot) {
            if (descriptors[slot].revents == 0) continue;
            x_client_service(&server, &server.clients[client_slots[slot]]);
        }
        if (descriptors[0].revents != 0) {
            int accepted = accept(server.listen_fd, NULL, NULL);
            if (accepted >= 0) {
                printf("xserver: accepted a client connection\n");
                fflush(stdout);
                x_client_accept(&server, accepted);
            }
        }
        x_server_flush_clients(&server);
    }

    (void)akaos_toolkit_disconnect(server.channel);
    return 0;
}
