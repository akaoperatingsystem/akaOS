/* Connection setup reply construction for the akaOS X11 display server.
 *
 * The layout follows the X Window System connection setup reply:
 *
 *   1  success
 *   1  unused
 *   2  protocol major version
 *   2  protocol minor version
 *   2  length of the additional data in four byte units
 *   4  release number
 *   4  resource id base
 *   4  resource id mask
 *   4  motion buffer size
 *   2  length of the vendor string
 *   2  maximum request length
 *   1  number of screens
 *   1  number of pixmap formats
 *   1  image byte order
 *   1  bitmap format bit order
 *   1  bitmap format scanline unit
 *   1  bitmap format scanline pad
 *   1  minimum key code
 *   1  maximum key code
 *   4  unused
 *   n  vendor string, padded to a four byte boundary
 *   .. pixmap formats
 *   .. screens
 */
#include "setup.h"
#include "proto.h"

#include <string.h>

#define X_SETUP_MAJOR 11
#define X_SETUP_MINOR 0
#define X_SETUP_ROOT_VISUAL 0x21u
#define X_SETUP_ROOT_DEPTH 24u
#define X_SETUP_MAX_REQUEST 65535u
#define X_SETUP_MIN_KEYCODE 8u
#define X_SETUP_MAX_KEYCODE 255u

static uint16_t order16(uint8_t byte_order, uint16_t value) {
    if (byte_order != 0x42) return value;
    return (uint16_t)((value >> 8) | (value << 8));
}

static uint32_t order32(uint8_t byte_order, uint32_t value) {
    if (byte_order != 0x42) return value;
    return ((value & 0x000000ffU) << 24) | ((value & 0x0000ff00U) << 8) |
           ((value & 0x00ff0000U) >> 8) | ((value & 0xff000000U) >> 24);
}

size_t x_setup_build(uint8_t *out, size_t capacity, int width, int height,
                      uint32_t resource_base, uint32_t resource_mask,
                      uint8_t byte_order, const char *vendor) {
    if (!out || capacity < 512U) return 0U;
    size_t vendor_length = vendor ? strlen(vendor) : 0U;

    uint8_t body[512];
    struct x_writer writer;
    x_writer_init(&writer, body, sizeof(body));

    x_writer_u32(&writer, order32(byte_order, 0U));              /* release */
    x_writer_u32(&writer, order32(byte_order, resource_base));
    x_writer_u32(&writer, order32(byte_order, resource_mask));
    x_writer_u32(&writer, order32(byte_order, 0U));              /* motion */
    x_writer_u16(&writer, order16(byte_order, (uint16_t)vendor_length));
    x_writer_u16(&writer, order16(byte_order, X_SETUP_MAX_REQUEST));
    x_writer_u8(&writer, 1U);                                   /* screens */
    x_writer_u8(&writer, 1U);                                   /* formats */
    x_writer_u8(&writer, byte_order == 0x42 ? 0x42 : 0x6c);     /* image */
    x_writer_u8(&writer, 0U);                                   /* bit order */
    x_writer_u8(&writer, 32U);                                  /* unit */
    x_writer_u8(&writer, 32U);                                  /* pad */
    x_writer_u8(&writer, X_SETUP_MIN_KEYCODE);
    x_writer_u8(&writer, X_SETUP_MAX_KEYCODE);
    x_writer_u32(&writer, 0U);                                  /* unused */

    for (size_t index = 0U; index < vendor_length; ++index) {
        x_writer_u8(&writer, (uint8_t)vendor[index]);
    }
    x_writer_pad(&writer, 4U);
    /* One pixmap format: depth 24 at 32 bits per pixel. */
    x_writer_u8(&writer, X_SETUP_ROOT_DEPTH);
    x_writer_u8(&writer, 32U);
    x_writer_u8(&writer, 32U);
    x_writer_u8(&writer, 0U);
    x_writer_u32(&writer, 0U);

    /* One screen. */
    x_writer_u32(&writer, order32(byte_order, 1U));              /* root */
    x_writer_u32(&writer, order32(byte_order, 0U));              /* colormap */
    x_writer_u32(&writer, order32(byte_order, 0x00ffffffU));     /* white */
    x_writer_u32(&writer, order32(byte_order, 0x00000000U));     /* black */
    x_writer_u32(&writer, order32(byte_order, 0U));              /* event mask */
    x_writer_u16(&writer, order16(byte_order, (uint16_t)width));
    x_writer_u16(&writer, order16(byte_order, (uint16_t)height));
    x_writer_u16(&writer, order16(byte_order, (uint16_t)(width * 254 / 96)));
    x_writer_u16(&writer, order16(byte_order, (uint16_t)(height * 254 / 96)));
    x_writer_u16(&writer, order16(byte_order, 1U));              /* min maps */
    x_writer_u16(&writer, order16(byte_order, 1U));              /* max maps */
    x_writer_u32(&writer, order32(byte_order, X_SETUP_ROOT_VISUAL));
    x_writer_u8(&writer, 0U);                                   /* backing */
    x_writer_u8(&writer, 0U);                                   /* save under */
    x_writer_u8(&writer, X_SETUP_ROOT_DEPTH);
    x_writer_u8(&writer, 1U);                                   /* depths */

    /* One depth with one visual. */
    x_writer_u8(&writer, X_SETUP_ROOT_DEPTH);
    x_writer_u8(&writer, 0U);
    x_writer_u16(&writer, order16(byte_order, 1U));              /* visuals */
    x_writer_u32(&writer, 0U);
    x_writer_u32(&writer, order32(byte_order, X_SETUP_ROOT_VISUAL));
    x_writer_u8(&writer, 4U);                                   /* TrueColor */
    x_writer_u8(&writer, 8U);                                   /* bits/rgb */
    x_writer_u16(&writer, order16(byte_order, 256U));           /* entries */
    x_writer_u32(&writer, order32(byte_order, 0x00ff0000U));
    x_writer_u32(&writer, order32(byte_order, 0x0000ff00U));
    x_writer_u32(&writer, order32(byte_order, 0x000000ffU));
    x_writer_u32(&writer, 0U);

    size_t additional = writer.position;
    if (additional % 4U != 0U) return 0U;
    if (8U + additional > capacity) return 0U;

    out[0] = 1U;                       /* success */
    out[1] = 0U;                       /* unused */
    uint16_t major = order16(byte_order, X_SETUP_MAJOR);
    uint16_t minor = order16(byte_order, X_SETUP_MINOR);
    uint16_t units = order16(byte_order, (uint16_t)(additional / 4U));
    memcpy(out + 2, &major, 2);
    memcpy(out + 4, &minor, 2);
    memcpy(out + 6, &units, 2);
    memcpy(out + 8, body, additional);
    return 8U + additional;
}
