/* Core X11 requests that a desktop environment needs and the first server
 * generation did not implement.
 *
 * The requests here cover coordinates, colormaps, grabs, the pointer and the
 * core font protocol.  They are the ones an unmodified window manager and an
 * unmodified GTK application use while they start up.
 */
#include <stdio.h>
#include <string.h>

#include <font8x16_data.h>

#include "server.h"
#include "xcore.h"

/* ---------------------------------------------------------------- core fonts
 *
 * The core font protocol needs a server side font.  The display server uses
 * the same 8x16 VGA bitmap font that the console uses, so a window manager
 * that falls back to core fonts draws the same glyphs the console shows. */
#define X_FONT_CELL_WIDTH 8
#define X_FONT_CELL_HEIGHT 16
#define X_FONT_ASCENT 12
#define X_FONT_DESCENT 4
#define X_FONT_FIRST_CHAR 32
#define X_FONT_LAST_CHAR 126
#define X_FONT_CHAR_COUNT (X_FONT_LAST_CHAR - X_FONT_FIRST_CHAR + 1)

/* Write one glyph with its baseline at the given row. */
static void draw_glyph(struct x_server *server, xid_t drawable, int16_t x,
                       int16_t baseline, uint8_t character, uint32_t pixel) {
    if (character < X_FONT_FIRST_CHAR || character > X_FONT_LAST_CHAR) {
        character = (uint8_t)'?';
    }
    const uint8_t *glyph = akaos_vga_font + (size_t)character * X_FONT_CELL_HEIGHT;
    for (int row = 0; row < X_FONT_CELL_HEIGHT; ++row) {
        uint8_t bits = glyph[row];
        if (bits == 0U) continue;
        int16_t target_y = (int16_t)(baseline - X_FONT_ASCENT + row);
        for (int column = 0; column < X_FONT_CELL_WIDTH; ++column) {
            if ((bits & (0x80U >> column)) == 0U) continue;
            x_drawable_put_pixel(server, drawable, (int16_t)(x + column), target_y,
                                 pixel);
        }
    }
}

/* Look a graphics context up for the font requests. */
static struct x_gc *find_font_gc(struct x_server *server, uint32_t gc_id) {
    for (int index = 0; index < X_MAX_GCS; ++index) {
        if (server->gcs[index].id == gc_id) return &server->gcs[index];
    }
    return NULL;
}

/* The character information the core font protocol reports for a glyph. */
static void put_char_info(struct x_client *client, uint8_t *buffer, size_t offset,
                          int16_t left_bearing, int16_t right_bearing,
                          int16_t width, int16_t ascent, int16_t descent) {
    int16_t values[5];
    values[0] = left_bearing;
    values[1] = right_bearing;
    values[2] = width;
    values[3] = ascent;
    values[4] = descent;
    for (int index = 0; index < 5; ++index) {
        uint16_t swapped = client_u16(client, (uint16_t)values[index]);
        memcpy(buffer + offset + (size_t)index * 2U, &swapped, 2);
    }
    uint16_t attributes = client_u16(client, 0U);
    memcpy(buffer + offset + 10U, &attributes, 2);
}

void x_core_query_font(struct x_server *server, struct x_client *client,
                       uint32_t font_id) {
    (void)server;
    int found = 0;
    for (int index = 0; index < X_MAX_FONTS; ++index) {
        if (server->fonts[index].id == font_id) {
            found = 1;
            break;
        }
    }
    if (!found) {
        x_reply_error(client, BadFont, 0, font_id);
        return;
    }

    /* The fixed part of the reply holds the bounds and the character range,
     * and the character information and the name of the font follow it. */
    static const char *font_name = "fixed";
    size_t name_length = strlen(font_name);
    /* A string of the protocol is one length byte, the characters and then
     * padding so that the whole item takes a multiple of four bytes. */
    size_t name_bytes = (name_length + 1U + 3U) & ~(size_t)3U;
    uint32_t char_infos = (uint32_t)X_FONT_CHAR_COUNT;
    uint32_t total = 60U + char_infos * 12U + (uint32_t)name_bytes;

    uint8_t buffer[64];
    memset(buffer, 0, sizeof(buffer));
    buffer[0] = X_Reply;
    buffer[1] = 0U;
    uint16_t sequence = client_u16(client, (uint16_t)client->sequence);
    memcpy(buffer + 2, &sequence, 2);
    uint32_t length = (total - 32U) / 4U;
    uint32_t swapped_length = client_u32(client, length);
    memcpy(buffer + 4, &swapped_length, 4);

    put_char_info(client, buffer, 8, 0, X_FONT_CELL_WIDTH, X_FONT_CELL_WIDTH,
                  X_FONT_ASCENT, X_FONT_DESCENT);
    put_char_info(client, buffer, 24, 0, X_FONT_CELL_WIDTH, X_FONT_CELL_WIDTH,
                  X_FONT_ASCENT, X_FONT_DESCENT);

    uint16_t values[4];
    values[0] = (uint16_t)X_FONT_FIRST_CHAR;   /* the lowest character */
    values[1] = (uint16_t)X_FONT_LAST_CHAR;    /* the highest character */
    values[2] = (uint16_t)' ';                 /* the default character */
    values[3] = 0U;                            /* the number of font properties */
    for (int index = 0; index < 4; ++index) {
        uint16_t swapped = client_u16(client, values[index]);
        memcpy(buffer + 40U + (size_t)index * 2U, &swapped, 2);
    }
    buffer[48] = 0U;                      /* the draw direction */
    buffer[49] = 0U;                      /* the lowest byte of a character */
    buffer[50] = 255U;                    /* the highest byte of a character */
    buffer[51] = 1U;                      /* every character exists */
    uint16_t ascent = client_u16(client, (uint16_t)X_FONT_ASCENT);
    uint16_t descent = client_u16(client, (uint16_t)X_FONT_DESCENT);
    memcpy(buffer + 52, &ascent, 2);
    memcpy(buffer + 54, &descent, 2);
    uint32_t infos = client_u32(client, char_infos);
    memcpy(buffer + 56, &infos, 4);

    x_reply_bytes(client, buffer, 60U);
    for (uint32_t index = 0U; index < char_infos; ++index) {
        uint8_t info[12];
        put_char_info(client, info, 0, 0, X_FONT_CELL_WIDTH, X_FONT_CELL_WIDTH,
                      X_FONT_ASCENT, X_FONT_DESCENT);
        x_reply_bytes(client, info, sizeof(info));
    }
    /* The name of the font is the last item of the reply. */
    uint8_t name[16];
    memset(name, 0, sizeof(name));
    name[0] = (uint8_t)name_length;
    memcpy(name + 1, font_name, name_length);
    x_reply_bytes(client, name, name_bytes);
}

void x_core_query_text_extents(struct x_server *server, struct x_client *client,
                               uint32_t font_id, const uint16_t *characters,
                               size_t count) {
    (void)server;
    int found = 0;
    for (int index = 0; index < X_MAX_FONTS; ++index) {
        if (server->fonts[index].id == font_id) {
            found = 1;
            break;
        }
    }
    if (!found) {
        x_reply_error(client, BadFont, 0, font_id);
        return;
    }

    uint8_t buffer[32];
    memset(buffer, 0, sizeof(buffer));
    buffer[0] = X_Reply;
    buffer[1] = 0U;
    uint16_t sequence = client_u16(client, (uint16_t)client->sequence);
    memcpy(buffer + 2, &sequence, 2);
    uint32_t length = client_u32(client, 0U);
    memcpy(buffer + 4, &length, 4);
    int16_t values[6];
    values[0] = 0;                        /* the draw direction */
    values[1] = X_FONT_ASCENT;
    values[2] = X_FONT_DESCENT;
    values[3] = 0;                        /* the overall left side bearing */
    values[4] = (int16_t)(count * X_FONT_CELL_WIDTH);
    values[5] = 0;                        /* the overall character width */
    for (int index = 0; index < 6; ++index) {
        uint16_t swapped = client_u16(client, (uint16_t)values[index]);
        memcpy(buffer + 8U + (size_t)index * 2U, &swapped, 2);
    }
    (void)characters;
    x_reply_bytes(client, buffer, sizeof(buffer));
}

void x_core_poly_text(struct x_server *server, struct x_client *client,
                      uint32_t drawable, uint32_t gc_id, int16_t x, int16_t y,
                      const struct x_text_item *items, size_t item_count) {
    struct x_gc *gc = find_font_gc(server, gc_id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, gc_id);
        return;
    }
    int drawable_index = x_window_index(server, drawable);
    if (drawable_index < 0 && !x_pixmap_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    /* Every item carries the offset from the position of the previous item,
     * and the characters of an item advance the position by their width. */
    int16_t cursor_x = x;
    int16_t cursor_y = y;
    int32_t left = x;
    int32_t right = x;
    int32_t top = y;
    int32_t bottom = y;
    for (size_t item = 0U; item < item_count; ++item) {
        cursor_x = (int16_t)(cursor_x + items[item].delta_x);
        cursor_y = (int16_t)(cursor_y + items[item].delta_y);
        for (size_t index = 0U; index < items[item].length; ++index) {
            draw_glyph(server, drawable, cursor_x, cursor_y, items[item].text[index],
                       gc->foreground);
            cursor_x = (int16_t)(cursor_x + X_FONT_CELL_WIDTH);
        }
        if (items[item].length > 0U) {
            int32_t item_right = (int32_t)cursor_x;
            if (item_right > right) right = item_right;
            if ((int32_t)cursor_y < top) top = cursor_y;
            if ((int32_t)cursor_y > bottom) bottom = cursor_y;
        }
    }
    if (right > left) {
        x_server_damage(server, drawable_index, (int16_t)left,
                        (int16_t)(top - X_FONT_ASCENT),
                        (uint16_t)(right - left),
                        (uint16_t)(bottom - top + X_FONT_ASCENT + X_FONT_DESCENT));
    }
    x_server_repaint(server);
}

void x_core_image_text(struct x_server *server, struct x_client *client,
                       uint32_t drawable, uint32_t gc_id, int16_t x, int16_t y,
                       const uint8_t *text, size_t length) {
    struct x_gc *gc = find_font_gc(server, gc_id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, gc_id);
        return;
    }
    int drawable_index = x_window_index(server, drawable);
    if (drawable_index < 0 && !x_pixmap_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    /* The image text request paints the background over the whole string
     * before it draws the characters. */
    uint16_t width = (uint16_t)(length * X_FONT_CELL_WIDTH);
    uint16_t height = (uint16_t)(X_FONT_ASCENT + X_FONT_DESCENT);
    x_drawable_fill(server, drawable, x, (int16_t)(y - X_FONT_ASCENT), width,
                    height, gc->background);
    for (size_t index = 0U; index < length; ++index) {
        draw_glyph(server, drawable, (int16_t)(x + (int16_t)index * X_FONT_CELL_WIDTH),
                   y, text[index], gc->foreground);
    }
    x_server_damage(server, drawable_index, x, (int16_t)(y - X_FONT_ASCENT), width,
                    height);
    x_server_repaint(server);
}

/* ------------------------------------------------------------------ colors */
struct x_color_name {
    const char *name;
    uint16_t red;
    uint16_t green;
    uint16_t blue;
};

/* The colour names that the core protocol has to understand.  The values are
 * the sixteen bit forms the protocol uses. */
static const struct x_color_name color_names[] = {
    {"black", 0U, 0U, 0U},
    {"white", 65535U, 65535U, 65535U},
    {"red", 65535U, 0U, 0U},
    {"green", 0U, 65535U, 0U},
    {"blue", 0U, 0U, 65535U},
    {"cyan", 0U, 65535U, 65535U},
    {"magenta", 65535U, 0U, 65535U},
    {"yellow", 65535U, 65535U, 0U},
    {"gray", 32896U, 32896U, 32896U},
    {"grey", 32896U, 32896U, 32896U},
    {"lightgray", 51450U, 51450U, 51450U},
    {"lightgrey", 51450U, 51450U, 51450U},
    {"darkgray", 8224U, 8224U, 8224U},
    {"darkgrey", 8224U, 8224U, 8224U},
    {"orange", 65535U, 32896U, 0U},
    {"brown", 32896U, 16448U, 0U},
    {"pink", 65535U, 49275U, 52185U},
    {"purple", 32896U, 0U, 32896U},
    {"navy", 0U, 0U, 32896U},
    {"maroon", 32896U, 0U, 0U},
    {"olive", 32896U, 32896U, 0U},
    {"teal", 0U, 32896U, 32896U},
    {"lime", 0U, 65535U, 0U},
    {"aqua", 0U, 65535U, 65535U},
    {"fuchsia", 65535U, 0U, 65535U},
    {"silver", 51450U, 51450U, 51450U},
    {"transparent", 0U, 0U, 0U},
};

static int lookup_color_name(const char *name, size_t length, uint16_t *red,
                             uint16_t *green, uint16_t *blue) {
    for (size_t index = 0U;
         index < sizeof(color_names) / sizeof(color_names[0]); ++index) {
        const struct x_color_name *entry = &color_names[index];
        if (strlen(entry->name) != length) continue;
        if (memcmp(entry->name, name, length) != 0) continue;
        if (red) *red = entry->red;
        if (green) *green = entry->green;
        if (blue) *blue = entry->blue;
        return 1;
    }
    return 0;
}

/* The screen uses a twenty four bit true colour visual, so a colour
 * component is scaled into sixteen bits and packed into the pixel. */
static uint32_t pack_pixel(uint16_t red, uint16_t green, uint16_t blue) {
    uint32_t r = ((uint32_t)red * 255U + 32767U) / 65535U;
    uint32_t g = ((uint32_t)green * 255U + 32767U) / 65535U;
    uint32_t b = ((uint32_t)blue * 255U + 32767U) / 65535U;
    return (r << 16U) | (g << 8U) | b;
}

void x_core_create_colormap(struct x_server *server, struct x_client *client,
                            uint32_t id, uint32_t window_id, uint32_t visual) {
    int index = x_window_index(server, window_id);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, window_id);
        return;
    }
    if (id == 0U) {
        x_reply_error(client, BadIDChoice, 0, id);
        return;
    }
    for (int slot = 0; slot < X_MAX_COLORMAPS; ++slot) {
        if (server->colormaps[slot].id == id) {
            x_reply_error(client, BadIDChoice, 0, id);
            return;
        }
    }
    int free_slot = -1;
    for (int slot = 0; slot < X_MAX_COLORMAPS; ++slot) {
        if (server->colormaps[slot].id == 0U) {
            free_slot = slot;
            break;
        }
    }
    if (free_slot < 0) {
        x_reply_error(client, BadAlloc, 0, 0U);
        return;
    }
    struct x_colormap *colormap = &server->colormaps[free_slot];
    memset(colormap, 0, sizeof(*colormap));
    colormap->id = id;
    colormap->client = client_index_of(server, client);
    colormap->visual_id = visual;
    colormap->window = window_id;
    /* The core protocol creates a colormap without a reply. */
}

void x_core_free_colormap(struct x_server *server, uint32_t id) {
    for (int slot = 0; slot < X_MAX_COLORMAPS; ++slot) {
        if (server->colormaps[slot].id != id) continue;
        memset(&server->colormaps[slot], 0, sizeof(server->colormaps[slot]));
        return;
    }
}

void x_core_alloc_color(struct x_server *server, struct x_client *client,
                        uint32_t colormap_id, uint16_t red, uint16_t green,
                        uint16_t blue) {
    (void)server;
    (void)colormap_id;
    xAllocColorReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    reply.red = (CARD16)client_u16(client, red);
    reply.green = (CARD16)client_u16(client, green);
    reply.blue = (CARD16)client_u16(client, blue);
    reply.pixel = (CARD32)client_u32(client, pack_pixel(red, green, blue));
    x_reply_bytes(client, &reply, sizeof(reply));
}

void x_core_alloc_named_color(struct x_server *server, struct x_client *client,
                              uint32_t colormap_id, const char *name,
                              size_t length) {
    (void)server;
    (void)colormap_id;
    uint16_t red = 0U;
    uint16_t green = 0U;
    uint16_t blue = 0U;
    if (!lookup_color_name(name, length, &red, &green, &blue)) {
        x_reply_error(client, BadName, 0, 0U);
        return;
    }
    xAllocNamedColorReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    reply.pixel = (CARD32)client_u32(client, pack_pixel(red, green, blue));
    reply.exactRed = (CARD16)client_u16(client, red);
    reply.exactGreen = (CARD16)client_u16(client, green);
    reply.exactBlue = (CARD16)client_u16(client, blue);
    reply.screenRed = (CARD16)client_u16(client, red);
    reply.screenGreen = (CARD16)client_u16(client, green);
    reply.screenBlue = (CARD16)client_u16(client, blue);
    x_reply_bytes(client, &reply, sizeof(reply));
}

void x_core_lookup_color(struct x_server *server, struct x_client *client,
                         uint32_t colormap_id, const char *name, size_t length) {
    (void)server;
    (void)colormap_id;
    uint16_t red = 0U;
    uint16_t green = 0U;
    uint16_t blue = 0U;
    if (!lookup_color_name(name, length, &red, &green, &blue)) {
        x_reply_error(client, BadName, 0, 0U);
        return;
    }
    xLookupColorReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    reply.exactRed = (CARD16)client_u16(client, red);
    reply.exactGreen = (CARD16)client_u16(client, green);
    reply.exactBlue = (CARD16)client_u16(client, blue);
    reply.screenRed = (CARD16)client_u16(client, red);
    reply.screenGreen = (CARD16)client_u16(client, green);
    reply.screenBlue = (CARD16)client_u16(client, blue);
    x_reply_bytes(client, &reply, sizeof(reply));
}

void x_core_query_colors(struct x_server *server, struct x_client *client,
                         uint32_t colormap_id, const uint32_t *pixels,
                         size_t count) {
    (void)server;
    (void)colormap_id;
    xQueryColorsReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)(count * 2U);
    reply.nColors = (CARD16)client_u16(client, (uint16_t)count);
    x_reply_bytes(client, &reply, sizeof(reply));
    for (size_t index = 0U; index < count; ++index) {
        uint32_t pixel = pixels[index];
        uint16_t red = (uint16_t)(((pixel >> 16U) & 0xffU) * 257U);
        uint16_t green = (uint16_t)(((pixel >> 8U) & 0xffU) * 257U);
        uint16_t blue = (uint16_t)((pixel & 0xffU) * 257U);
        uint8_t entry[8];
        memcpy(entry, &(CARD16){client_u16(client, red)}, 2);
        memcpy(entry + 2, &(CARD16){client_u16(client, green)}, 2);
        memcpy(entry + 4, &(CARD16){client_u16(client, blue)}, 2);
        entry[6] = 0U;
        entry[7] = 0U;
        x_reply_bytes(client, entry, sizeof(entry));
    }
}

/* -------------------------------------------------------------- coordinates */
void x_core_translate_coordinates(struct x_server *server,
                                  struct x_client *client,
                                  uint32_t source_id, uint32_t destination_id,
                                  int16_t source_x, int16_t source_y) {
    int source_index = x_window_index(server, source_id);
    int destination_index = x_window_index(server, destination_id);
    if (source_index < 0 || destination_index < 0) {
        x_reply_error(client, BadWindow, 0,
                      source_index < 0 ? source_id : destination_id);
        return;
    }
    int16_t source_origin_x = 0;
    int16_t source_origin_y = 0;
    int16_t destination_origin_x = 0;
    int16_t destination_origin_y = 0;
    x_window_absolute_origin(server, source_index, &source_origin_x,
                             &source_origin_y);
    x_window_absolute_origin(server, destination_index, &destination_origin_x,
                             &destination_origin_y);
    int32_t root_x = source_origin_x + source_x;
    int32_t root_y = source_origin_y + source_y;
    xTranslateCoordsReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sameScreen = 1U;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    reply.child = (Window)client_u32(client, 0U);
    reply.dstX = (INT16)client_u16(
        client, (uint16_t)(int16_t)(root_x - destination_origin_x));
    reply.dstY = (INT16)client_u16(
        client, (uint16_t)(int16_t)(root_y - destination_origin_y));
    x_reply_bytes(client, &reply, sizeof(reply));
}

void x_core_warp_pointer(struct x_server *server, struct x_client *client,
                         uint32_t source_id, int16_t source_x, int16_t source_y,
                         uint32_t destination_id, int16_t destination_x,
                         int16_t destination_y) {
    (void)client;
    (void)source_id;
    (void)source_x;
    (void)source_y;
    (void)destination_id;
    (void)destination_x;
    (void)destination_y;
    if (destination_id != 0U) {
        int index = x_window_index(server, destination_id);
        if (index < 0) return;
        int16_t origin_x = 0;
        int16_t origin_y = 0;
        x_window_absolute_origin(server, index, &origin_x, &origin_y);
        server->pointer_x = (int16_t)(origin_x + destination_x);
        server->pointer_y = (int16_t)(origin_y + destination_y);
    } else {
        server->pointer_x = destination_x;
        server->pointer_y = destination_y;
    }
    if (server->pointer_x < 0) server->pointer_x = 0;
    if (server->pointer_y < 0) server->pointer_y = 0;
    if (server->pointer_x >= server->width) server->pointer_x = server->width - 1;
    if (server->pointer_y >= server->height) server->pointer_y = server->height - 1;
    server->pointer_window =
        x_window_find_at(server, server->pointer_x, server->pointer_y);
    x_event_motion(server, server->pointer_x, server->pointer_y);
}

/* -------------------------------------------------------------------- grabs */
void x_core_grab_pointer(struct x_server *server, struct x_client *client) {
    (void)server;
    xGrabPointerReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.status = GrabSuccess;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    x_reply_bytes(client, &reply, sizeof(reply));
}

void x_core_grab_keyboard(struct x_server *server, struct x_client *client) {
    (void)server;
    uint8_t reply[32];
    memset(reply, 0, sizeof(reply));
    reply[0] = X_Reply;
    reply[1] = GrabSuccess;
    memcpy(reply + 2, &(CARD16){client_u16(client, (uint16_t)client->sequence)}, 2);
    x_reply_bytes(client, reply, sizeof(reply));
}

/* ---------------------------------------------------------------- selection */
void x_core_convert_selection(struct x_server *server, struct x_client *client,
                              uint32_t requestor, uint32_t selection,
                              uint32_t target, uint32_t property,
                              uint32_t time) {
    /* The core protocol answers a conversion with a selection event instead
     * of a reply.  When a live window owns the selection, the server sends
     * SelectionRequest to the owner's client; otherwise it sends
     * SelectionNotify with property None to the requester. */
    if (x_window_index(server, requestor) < 0) {
        x_reply_error(client, BadWindow, 0, requestor);
        return;
    }
    xid_t owner = 0U;
    for (int slot = 0; slot < X_MAX_SELECTIONS; ++slot) {
        if (!server->selections[slot].owner) continue;
        if (server->selections[slot].selection != selection) continue;
        owner = server->selections[slot].owner;
        break;
    }

    uint32_t stamp = time != 0U ? time : (uint32_t)(server_now_millis() & 0xffffffffU);
    if (owner != 0U) {
        int owner_index = x_window_index(server, owner);
        int owner_client = owner_index >= 0 ? server->windows[owner_index].client : -1;
        if (owner_client >= 0 && server->clients[owner_client].in_use) {
            struct x_client *receiver = &server->clients[owner_client];
            uint8_t request_event[32];
            memset(request_event, 0, sizeof(request_event));
            request_event[0] = SelectionRequest;
            memcpy(request_event + 2, &(CARD16){client_u16(receiver, (uint16_t)receiver->sequence)}, 2);
            memcpy(request_event + 4, &(CARD32){client_u32(receiver, stamp)}, 4);
            memcpy(request_event + 8, &(CARD32){client_u32(receiver, owner)}, 4);
            memcpy(request_event + 12, &(CARD32){client_u32(receiver, requestor)}, 4);
            memcpy(request_event + 16, &(CARD32){client_u32(receiver, selection)}, 4);
            memcpy(request_event + 20, &(CARD32){client_u32(receiver, target)}, 4);
            memcpy(request_event + 24, &(CARD32){client_u32(receiver, property)}, 4);
            x_event_queue(receiver, request_event, sizeof(request_event), 0U);
            return;
        }
    }

    uint8_t event[32];
    memset(event, 0, sizeof(event));
    event[0] = SelectionNotify;
    memcpy(event + 2, &(CARD16){client_u16(client, (uint16_t)client->sequence)}, 2);
    memcpy(event + 4, &(CARD32){client_u32(client, stamp)}, 4);
    memcpy(event + 8, &(CARD32){client_u32(client, requestor)}, 4);
    memcpy(event + 12, &(CARD32){client_u32(client, selection)}, 4);
    memcpy(event + 16, &(CARD32){client_u32(client, target)}, 4);
    memcpy(event + 20, &(CARD32){client_u32(client, 0U)}, 4);
    x_event_queue(client, event, sizeof(event), 0U);
}

/* --------------------------------------------------------- core font support */
void x_core_open_font(struct x_server *server, struct x_client *client,
                      uint32_t id, const char *name, size_t length) {
    if (id == 0U) {
        x_reply_error(client, BadIDChoice, 0, id);
        return;
    }
    int free_slot = -1;
    for (int slot = 0; slot < X_MAX_FONTS; ++slot) {
        if (server->fonts[slot].id == 0U) {
            free_slot = slot;
            break;
        }
    }
    if (free_slot < 0) {
        x_reply_error(client, BadAlloc, 0, 0U);
        return;
    }
    struct x_font *font = &server->fonts[free_slot];
    memset(font, 0, sizeof(*font));
    font->id = id;
    font->client = client_index_of(server, client);
    font->name_length = length < sizeof(font->name) ? length : sizeof(font->name) - 1U;
    memcpy(font->name, name, font->name_length);
    font->name[font->name_length] = '\0';
    /* The core protocol opens a font without a reply. */
}

void x_core_close_font(struct x_server *server, uint32_t id) {
    for (int slot = 0; slot < X_MAX_FONTS; ++slot) {
        if (server->fonts[slot].id != id) continue;
        memset(&server->fonts[slot], 0, sizeof(server->fonts[slot]));
        return;
    }
}

/* Encode a list of names the way the protocol wants them.
 *
 * A string in the protocol is one length byte, the characters and then
 * padding so that the whole item takes a multiple of four bytes.  The list
 * follows the reply, and the length of the reply counts it. */
size_t x_core_encode_name_list(uint8_t *buffer, size_t capacity,
                               const char *const *names, uint32_t count) {
    size_t used = 0U;
    for (uint32_t index = 0U; index < count; ++index) {
        size_t length = strlen(names[index]);
        if (used + 1U + length > capacity) break;
        buffer[used] = (uint8_t)length;
        memcpy(buffer + used + 1U, names[index], length);
        used += 1U + length;
        while ((used & 3U) != 0U && used < capacity) {
            buffer[used] = 0U;
            ++used;
        }
    }
    return used;
}

void x_core_list_fonts(struct x_server *server, struct x_client *client) {
    (void)server;
    static const char *fonts[] = {"fixed"};
    uint32_t count = (uint32_t)(sizeof(fonts) / sizeof(fonts[0]));

    uint8_t list[256];
    size_t list_length = x_core_encode_name_list(list, sizeof(list), fonts, count);

    xListFontsReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.nFonts = (CARD16)client_u16(client, (uint16_t)count);
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)(list_length / 4U);
    x_reply_bytes(client, &reply, sizeof(reply));
    if (list_length) {
        x_reply_bytes(client, list, list_length);
    }
}

void x_core_get_font_path(struct x_server *server, struct x_client *client) {
    (void)server;
    static const char *path = "/usr/share/fonts";
    size_t length = strlen(path);
    size_t padded = (length + 3U) & ~(size_t)3U;
    xGetFontPathReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.nPaths = (CARD16)client_u16(client, 1U);
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)(padded / 4U);
    x_reply_bytes(client, &reply, sizeof(reply));
    uint8_t buffer[256];
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, path, length);
    x_reply_bytes(client, buffer, padded);
}
