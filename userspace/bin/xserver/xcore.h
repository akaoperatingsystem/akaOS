/* Core X11 request handlers that live in their own translation unit.
 *
 * The dispatcher includes this header so that it can call the handlers, and
 * the core request unit includes it as well.
 */
#ifndef AKAOS_XSERVER_XCORE_H
#define AKAOS_XSERVER_XCORE_H

#include "server.h"

/* Encode a list of names the way the protocol wants them and return the
 * number of bytes it took. */
size_t x_core_encode_name_list(uint8_t *buffer, size_t capacity,
                               const char *const *names, uint32_t count);

void x_core_query_font(struct x_server *server, struct x_client *client,
                       uint32_t font_id);void x_core_query_text_extents(struct x_server *server, struct x_client *client,
                               uint32_t font_id, const uint16_t *characters,
                               size_t count);

/* One item of a PolyText request.
 *
 * The item carries the offset from the position of the previous item and the
 * characters of the item.  The characters are always eight bit wide, because
 * the server draws them with the same cell font in both variants. */
struct x_text_item {
    int8_t delta_x;
    int8_t delta_y;
    const uint8_t *text;
    size_t length;
};

/* The bounds of the text item list of a PolyText request. */
#define X_MAX_TEXT_ITEMS 128U
#define X_MAX_TEXT_BYTES 8192U

void x_core_poly_text(struct x_server *server, struct x_client *client,
                      uint32_t drawable, uint32_t gc_id, int16_t x, int16_t y,
                      const struct x_text_item *items, size_t item_count);
void x_core_image_text(struct x_server *server, struct x_client *client,
                       uint32_t drawable, uint32_t gc_id, int16_t x, int16_t y,
                       const uint8_t *text, size_t length);

#endif
