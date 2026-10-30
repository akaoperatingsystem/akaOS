/* Connection setup reply construction for the akaOS X11 display server. */
#ifndef AKAOS_XSERVER_SETUP_H
#define AKAOS_XSERVER_SETUP_H

#include <stddef.h>
#include <stdint.h>

/* Build the X11 connection setup reply.
 *
 * The reply is the eight byte header followed by the additional data that
 * the header counts in four byte units.  The whole reply is written in the
 * byte order the client asked for.
 *
 * Returns the number of bytes written, or zero when the buffer is too
 * small. */
size_t x_setup_build(uint8_t *out, size_t capacity, int width, int height,
                      uint32_t resource_base, uint32_t resource_mask,
                      uint8_t byte_order, const char *vendor);

#endif
