/* Host side validation of the akaOS X11 connection setup reply.
 *
 * The reply is built by the same code the display server uses and is then
 * handed to the system libxcb, so the layout is checked by the same parser
 * that real X clients use.  This keeps the setup exchange verifiable without
 * rebuilding and booting a guest.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <xcb/xcb.h>

#include "setup.h"

int main(void) {
    uint8_t buffer[1024];
    size_t length = x_setup_build(buffer, sizeof(buffer), 1024, 768,
                                  0x20000000U, 0x00ffffffU, 0x6c,
                                  "The akaOS Project");
    if (length == 0U) {
        printf("FAIL: the setup reply was not built\n");
        return 1;
    }
    printf("setup reply is %zu bytes\n", length);
    printf("header:");
    for (size_t index = 0U; index < 16U && index < length; ++index) {
        printf(" %02x", buffer[index]);
    }
    printf("\n");
    fflush(stdout);

    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        perror("socketpair");
        return 1;
    }
    if (write(sockets[1], buffer, length) != (ssize_t)length) {
        perror("write");
        return 1;
    }
    {
        int pending = 0;
        if (ioctl(sockets[0], FIONREAD, &pending) == 0) {
            printf("bytes waiting on the socket: %d\n", pending);
        }
        fflush(stdout);
    }
    alarm(10);

    /* Prove that the reply is readable with the same primitives libxcb uses
     * before handing the socket to libxcb. */
    {
        int probe[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, probe) == 0) {
            if (write(probe[1], buffer, length) == (ssize_t)length) {
                uint8_t header[8];
                ssize_t got = recv(probe[0], header, sizeof(header), 0);
                printf("probe read %zd bytes, length field %u\n", got,
                       got == 8 ? (unsigned)(header[6] | (header[7] << 8)) : 0U);
                uint16_t units = (uint16_t)(header[6] | (header[7] << 8));
                uint8_t *rest = malloc((size_t)units * 4U);
                ssize_t rest_got = recv(probe[0], rest, (size_t)units * 4U, 0);
                printf("probe read %zd of %u additional bytes\n", rest_got,
                       (unsigned)units * 4U);
                free(rest);
                close(probe[0]);
                close(probe[1]);
            }
            fflush(stdout);
        }
    }

    xcb_connection_t *connection = xcb_connect_to_fd(sockets[0], NULL);
    if (!connection) {
        printf("FAIL: libxcb could not use the connection\n");
        return 1;
    }
    int error = xcb_connection_has_error(connection);
    if (error) {
        printf("FAIL: libxcb reported connection error %d\n", error);
        return 1;
    }
    const xcb_setup_t *setup = xcb_get_setup(connection);
    if (!setup) {
        printf("FAIL: libxcb returned no setup\n");
        return 1;
    }
    printf("status=%u length=%u\n", setup->status, setup->length);
    printf("rid_base=0x%x rid_mask=0x%x\n", setup->resource_id_base,
           setup->resource_id_mask);
    printf("roots=%u formats=%u vendor_len=%u\n", setup->roots_len,
           setup->pixmap_formats_len, setup->vendor_len);
    const xcb_screen_t *screen = xcb_setup_roots_iterator(setup).data;
    if (!screen) {
        printf("FAIL: libxcb found no screen\n");
        return 1;
    }
    printf("root=0x%x depth=%u width=%u height=%u\n", screen->root,
           screen->root_depth, screen->width_in_pixels,
           screen->height_in_pixels);
    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
    for (; iterator.rem; xcb_screen_next(&iterator)) {
        xcb_depth_iterator_t depths = xcb_screen_allowed_depths_iterator(
            iterator.data);
        for (; depths.rem; xcb_depth_next(&depths)) {
            xcb_visualtype_iterator_t visuals =
                xcb_depth_visuals_iterator(depths.data);
            for (; visuals.rem; xcb_visualtype_next(&visuals)) {
                printf("visual id=0x%x class=%u depth=%u\n",
                       visuals.data->visual_id, visuals.data->_class,
                       depths.data->depth);
            }
        }
    }
    xcb_disconnect(connection);
    printf("PASS: libxcb accepted the akaOS connection setup reply\n");
    fflush(stdout);
    /* The libxcb reader thread stays blocked on the socket, so leave without
     * waiting for it. */
    _exit(0);
}
