/* Extensions of the akaOS X11 display server.
 *
 * The file holds the RENDER, XFIXES and X-DAMAGE extensions.  A modern
 * toolkit needs them: a window manager renders its decorations with RENDER
 * and composites them onto the frame of the window, a toolkit asks for
 * reports about changed areas with X-DAMAGE, and XFIXES carries the cursor
 * names and the window shapes.
 *
 * Every extension is found by the client libraries through the QueryExtension
 * request, so the major opcode only has to be stable and unique on the
 * server.
 */
#ifndef AKAOS_XSERVER_EXTENSIONS_INC
#define AKAOS_XSERVER_EXTENSIONS_INC

/* The picture formats of the server.
 *
 * A format is identified by a number of its own, and the client learns the
 * number from the answer of the RENDER extension. */
#define X_PICT_FORMAT_RGB24  0x30U
#define X_PICT_FORMAT_ARGB32 0x31U
#define X_PICT_FORMAT_A8     0x32U
#define X_PICT_FORMAT_A1     0x33U

/* The visual identifiers of the server.
 *
 * The root window uses the visual of depth 24, and the visual of depth 32
 * carries an alpha channel, which the window manager needs for the
 * decorations that are drawn with transparent pixels. */
#define X_VISUAL_RGB24  0x21U
#define X_VISUAL_ARGB32 0x22U

/* The composite operators of the RENDER extension. */
#define X_PICT_OP_CLEAR        0
#define X_PICT_OP_SRC          1
#define X_PICT_OP_DST          2
#define X_PICT_OP_OVER         3
#define X_PICT_OP_OVER_REVERSE 4
#define X_PICT_OP_IN           5
#define X_PICT_OP_IN_REVERSE   6
#define X_PICT_OP_OUT          7
#define X_PICT_OP_OUT_REVERSE  8
#define X_PICT_OP_ATOP         9
#define X_PICT_OP_ATOP_REVERSE 10
#define X_PICT_OP_XOR          11
#define X_PICT_OP_ADD          12
#define X_PICT_OP_SATURATE     13

/* ------------------------------------------------------------------ pictures */

/* Find a picture of the server by its identifier. */
static struct x_picture *x_picture_of(struct x_server *server, xid_t id) {
    if (id == 0U) return NULL;
    for (int slot = 0; slot < X_MAX_PICTURES; ++slot) {
        if (server->pictures[slot].id == id) return &server->pictures[slot];
    }
    return NULL;
}

/* Reserve a picture slot for a client that named the identifier itself. */
static struct x_picture *x_picture_create(struct x_server *server,
                                           struct x_client *client, xid_t id) {
    for (int slot = 0; slot < X_MAX_PICTURES; ++slot) {
        if (server->pictures[slot].id != 0U) continue;
        struct x_picture *picture = &server->pictures[slot];
        memset(picture, 0, sizeof(*picture));
        picture->id = id;
        picture->client = client_index_of(server, client);
        picture->graphics_exposures = 1U;
        picture->subpixel_order = 0U;
        return picture;
    }
    return NULL;
}

static void x_picture_free(struct x_server *server, xid_t id) {
    struct x_picture *picture = x_picture_of(server, id);
    if (!picture) return;
    memset(picture, 0, sizeof(*picture));
}

/* Whether a picture format carries an alpha channel. */
static int x_picture_format_has_alpha(uint32_t format) {
    return format == X_PICT_FORMAT_ARGB32 || format == X_PICT_FORMAT_A8 ||
           format == X_PICT_FORMAT_A1;
}

/* The depth of a picture format. */
static uint8_t x_picture_format_depth(uint32_t format) {
    switch (format) {
    case X_PICT_FORMAT_RGB24: return 24U;
    case X_PICT_FORMAT_ARGB32: return 32U;
    case X_PICT_FORMAT_A8: return 8U;
    case X_PICT_FORMAT_A1: return 1U;
    default: return 24U;
    }
}

/* ------------------------------------------------------------------- blending */

/* Combine a source pixel over a destination pixel.
 *
 * The source carries its own alpha channel, and the result keeps the alpha
 * channel of the destination, because the screen of the server holds pixels
 * without an alpha channel. */
static uint32_t x_picture_blend_over(uint32_t source, uint32_t destination) {
    uint32_t alpha = (source >> 24) & 0xffU;
    if (alpha == 0U) return destination;
    if (alpha == 0xffU) return (source & 0x00ffffffU) | 0xff000000U;
    uint32_t inverse = 255U - alpha;
    uint32_t source_red = (source >> 16) & 0xffU;
    uint32_t source_green = (source >> 8) & 0xffU;
    uint32_t source_blue = source & 0xffU;
    uint32_t destination_red = (destination >> 16) & 0xffU;
    uint32_t destination_green = (destination >> 8) & 0xffU;
    uint32_t destination_blue = destination & 0xffU;
    uint32_t red = (source_red * alpha + destination_red * inverse + 127U) / 255U;
    uint32_t green = (source_green * alpha + destination_green * inverse + 127U) / 255U;
    uint32_t blue = (source_blue * alpha + destination_blue * inverse + 127U) / 255U;
    return 0xff000000U | (red << 16) | (green << 8) | blue;
}

/* Combine a source pixel into a destination pixel with an operator. */
static uint32_t x_picture_combine(uint8_t operator_code, uint32_t source,
                                  uint32_t destination, uint32_t mask_value) {
    uint32_t source_alpha = (source >> 24) & 0xffU;
    switch (operator_code) {
    case X_PICT_OP_CLEAR:
        return 0U;
    case X_PICT_OP_SRC:
        return (source & 0x00ffffffU) | 0xff000000U;
    case X_PICT_OP_DST:
        return destination;
    case X_PICT_OP_OVER:
        return x_picture_blend_over(source, destination);
    case X_PICT_OP_OVER_REVERSE:
        return x_picture_blend_over(destination, source);
    case X_PICT_OP_IN:
        return x_picture_blend_over(source & 0x00ffffffU,
                                    destination & 0x00ffffffU) |
               (destination & 0xff000000U);
    case X_PICT_OP_OUT:
        return (destination & 0x00ffffffU) & ~(source & 0x00ffffffU);
    case X_PICT_OP_ATOP:
        return x_picture_blend_over(source, destination);
    case X_PICT_OP_XOR:
        return ((source ^ destination) & 0x00ffffffU) | 0xff000000U;
    case X_PICT_OP_ADD: {
        uint32_t red = ((source >> 16) & 0xffU) + ((destination >> 16) & 0xffU);
        uint32_t green = ((source >> 8) & 0xffU) + ((destination >> 8) & 0xffU);
        uint32_t blue = (source & 0xffU) + (destination & 0xffU);
        if (red > 255U) red = 255U;
        if (green > 255U) green = 255U;
        if (blue > 255U) blue = 255U;
        return 0xff000000U | (red << 16) | (green << 8) | blue;
    }
    case X_PICT_OP_SATURATE:
        return x_picture_blend_over(source, destination);
    default:
        (void)mask_value;
        (void)source_alpha;
        return destination;
    }
}

/* ------------------------------------------------------------------- RENDER */

/* The answer of the RENDER request that reports the picture formats.
 *
 * The answer carries the formats of the server, the depths of the screen and
 * the visuals of every depth. */
static void render_query_pict_formats(struct x_server *server,
                                      struct x_client *client) {
    (void)server;
    static const uint32_t formats[] = {
        X_PICT_FORMAT_RGB24,
        X_PICT_FORMAT_ARGB32,
        X_PICT_FORMAT_A8,
        X_PICT_FORMAT_A1,
    };
    const uint32_t format_count = (uint32_t)(sizeof(formats) / sizeof(formats[0]));

    /* Two depths, one visual each, and one subpixel order. */
    const uint32_t depth_count = 2U;
    const uint32_t visual_count = 2U;
    const uint32_t subpixel_count = 1U;

    size_t length = 0U;
    length += (size_t)format_count * 28U;          /* the picture formats */
    length += 8U;                                  /* the screen */
    length += (size_t)depth_count * 8U;            /* the depths */
    length += (size_t)visual_count * 8U;           /* the visuals */
    length += (size_t)subpixel_count * 4U;         /* the subpixel orders */

    uint8_t *payload = calloc(1U, length);
    if (!payload) {
        x_reply_error(client, BadAlloc, 0, 0U);
        return;
    }

    uint8_t header[32];
    memset(header, 0, sizeof(header));
    reply_begin(client, header, 0U, (uint32_t)(length / 4U));
    reply_put_u32(client, header, 8, format_count);
    reply_put_u32(client, header, 12, 1U);          /* numScreens */
    reply_put_u32(client, header, 16, depth_count);
    reply_put_u32(client, header, 20, visual_count);
    reply_put_u32(client, header, 24, subpixel_count);
    x_reply_bytes(client, header, sizeof(header));

    size_t offset = 0U;
    for (uint32_t index = 0U; index < format_count; ++index) {
        uint8_t *entry = payload + offset;
        offset += 28U;
        uint32_t format = formats[index];
        reply_put_u32(client, entry, 0, format);
        entry[4] = 1U;                             /* a picture of direct colour */
        entry[5] = x_picture_format_depth(format);
        reply_put_u16(client, entry, 8, 0xffffU);  /* red */
        reply_put_u16(client, entry, 10, 0xff00U); /* the mask of red */
        reply_put_u16(client, entry, 12, 0xffffU); /* green */
        reply_put_u16(client, entry, 14, 0x00ffU);
        reply_put_u16(client, entry, 16, 0xffffU); /* blue */
        reply_put_u16(client, entry, 18, 0x000fU);
        if (format == X_PICT_FORMAT_ARGB32) {
            reply_put_u16(client, entry, 20, 0xffffU); /* alpha */
            reply_put_u16(client, entry, 22, 0xff00U);
        }
        reply_put_u32(client, entry, 24, 0U);       /* the colormap */
    }

    /* One screen with two depths and the colour format as the fallback. */
    reply_put_u32(client, payload + offset, 0, depth_count);
    reply_put_u32(client, payload + offset, 4, X_PICT_FORMAT_RGB24);
    offset += 8U;

    /* The depth of twenty four bits with the visual of the root window. */
    {
        uint8_t *entry = payload + offset;
        offset += 8U;
        entry[0] = 24U;
        reply_put_u16(client, entry, 2, 1U);
    }
    /* The depth of thirty two bits with the visual that carries alpha. */
    {
        uint8_t *entry = payload + offset;
        offset += 8U;
        entry[0] = 32U;
        reply_put_u16(client, entry, 2, 1U);
    }

    /* The visual of depth twenty four and the visual of depth thirty two. */
    reply_put_u32(client, payload + offset, 0, X_VISUAL_RGB24);
    reply_put_u32(client, payload + offset, 4, X_PICT_FORMAT_RGB24);
    offset += 8U;
    reply_put_u32(client, payload + offset, 0, X_VISUAL_ARGB32);
    reply_put_u32(client, payload + offset, 4, X_PICT_FORMAT_ARGB32);
    offset += 8U;

    /* One subpixel order for horizontal stripes. */
    reply_put_u32(client, payload + offset, 0, 1U);

    x_reply_bytes(client, payload, length);
    free(payload);
}

/* Apply the value list of a picture to a picture.
 *
 * The values appear in the order of the bits of the mask, and every one of
 * them is aligned to four bytes. */
static void render_read_picture_values(struct x_client *client,
                                       struct x_reader *reader,
                                       uint32_t value_mask,
                                       struct x_picture *picture) {
    if (value_mask & (1U << 0)) {          /* CPRepeat */
        picture->repeat = rd_u8(reader);
        (void)x_reader_skip(reader, 3U);
    }
    if (value_mask & (1U << 1)) picture->alpha_map = rd_u32(client, reader);
    if (value_mask & (1U << 2)) (void)rd_i16(client, reader); /* alpha origin */
    if (value_mask & (1U << 3)) (void)rd_i16(client, reader);
    if (value_mask & (1U << 4)) (void)rd_i16(client, reader); /* clip origin */
    if (value_mask & (1U << 5)) (void)rd_i16(client, reader);
    if (value_mask & (1U << 6)) picture->clip_mask = rd_u32(client, reader);
    if (value_mask & (1U << 7)) {          /* CPGraphicsExposure */
        picture->graphics_exposures = rd_u8(reader);
        (void)x_reader_skip(reader, 3U);
    }
    if (value_mask & (1U << 8)) {          /* CPSubwindowMode */
        picture->subpixel_order = rd_u8(reader);
        (void)x_reader_skip(reader, 3U);
    }
    if (value_mask & (1U << 9)) (void)rd_u8(reader);
    if (value_mask & (1U << 10)) (void)rd_u8(reader);
    if (value_mask & (1U << 11)) (void)rd_u32(client, reader);
    if (value_mask & (1U << 12)) (void)rd_u8(reader);
}

/* Composite a picture onto another picture. */
static void render_composite(struct x_server *server, struct x_client *client,
                             struct x_reader *reader) {
    uint8_t operator_code = rd_u8(reader);
    (void)x_reader_skip(reader, 3U);
    uint32_t source_id = rd_u32(client, reader);
    uint32_t mask_id = rd_u32(client, reader);
    uint32_t destination_id = rd_u32(client, reader);
    int16_t source_x = rd_i16(client, reader);
    int16_t source_y = rd_i16(client, reader);
    (void)rd_i16(client, reader);   /* xMask */
    (void)rd_i16(client, reader);   /* yMask */
    int16_t destination_x = rd_i16(client, reader);
    int16_t destination_y = rd_i16(client, reader);
    uint16_t width = rd_u16(client, reader);
    uint16_t height = rd_u16(client, reader);

    struct x_picture *source_picture = x_picture_of(server, source_id);
    struct x_picture *destination_picture = x_picture_of(server, destination_id);
    if (!source_picture || !destination_picture) {
        x_reply_error(client, BadPicture, 0, 0U);
        return;
    }

    uint16_t source_width = 0U;
    uint16_t source_height = 0U;
    int source_is_window = 0;
    uint32_t *source_pixels =
        x_drawable_pixels(server, source_picture->drawable, &source_width,
                          &source_height, &source_is_window);
    uint16_t destination_width = 0U;
    uint16_t destination_height = 0U;
    int destination_is_window = 0;
    uint32_t *destination_pixels =
        x_drawable_pixels(server, destination_picture->drawable, &destination_width,
                          &destination_height, &destination_is_window);
    if (!source_pixels || !destination_pixels) {
        x_reply_error(client, BadDrawable, 0, 0U);
        return;
    }

    uint16_t mask_width = 0U;
    uint16_t mask_height = 0U;
    int mask_is_window = 0;
    uint32_t *mask_pixels = NULL;
    if (mask_id != 0U) {
        struct x_picture *mask_picture = x_picture_of(server, mask_id);
        if (mask_picture) {
            mask_pixels = x_drawable_pixels(server, mask_picture->drawable,
                                            &mask_width, &mask_height,
                                            &mask_is_window);
        }
    }

    int source_has_alpha = x_picture_format_has_alpha(source_picture->format);

    for (uint32_t row = 0U; row < height; ++row) {
        int32_t target_y = destination_y + (int32_t)row;
        if (target_y < 0 || target_y >= (int32_t)destination_height) continue;
        for (uint32_t column = 0U; column < width; ++column) {
            int32_t target_x = destination_x + (int32_t)column;
            if (target_x < 0 || target_x >= (int32_t)destination_width) continue;
            int32_t read_x = source_x + (int32_t)column;
            int32_t read_y = source_y + (int32_t)row;
            if (read_x < 0 || read_y < 0 || read_x >= (int32_t)source_width ||
                read_y >= (int32_t)source_height) {
                continue;
            }
            uint32_t value = source_pixels[(size_t)read_y * source_width +
                                           (size_t)read_x];
            if (!source_has_alpha) value |= 0xff000000U;

            uint32_t mask_value = 0xff000000U;
            if (mask_pixels) {
                int32_t mask_x = read_x;
                int32_t mask_y = read_y;
                if (mask_x >= 0 && mask_y >= 0 && mask_x < (int32_t)mask_width &&
                    mask_y < (int32_t)mask_height) {
                    mask_value = mask_pixels[(size_t)mask_y * mask_width +
                                             (size_t)mask_x];
                } else {
                    mask_value = 0U;
                }
            }

            /* The mask scales the alpha channel of the source. */
            uint32_t mask_alpha = (mask_value >> 24) & 0xffU;
            uint32_t alpha = (value >> 24) & 0xffU;
            alpha = (alpha * mask_alpha + 127U) / 255U;
            value = (value & 0x00ffffffU) | (alpha << 24);

            uint32_t destination_value =
                destination_pixels[(size_t)target_y * destination_width +
                                   (size_t)target_x];
            destination_pixels[(size_t)target_y * destination_width +
                               (size_t)target_x] =
                x_picture_combine(operator_code, value, destination_value,
                                  mask_value);
        }
    }

    /* The window that received the pixels changed, so the screen has to be
     * repainted and the clients that asked for reports have to learn about
     * the change. */
    int destination_index = x_window_index(server, destination_picture->drawable);
    if (destination_index >= 0) {
        x_server_damage(server, destination_index, destination_x, destination_y,
                        (int16_t)width, (int16_t)height);
        x_server_repaint(server);
    }
}

static void dispatch_render_request(struct x_server *server, struct x_client *client,
                                    struct x_reader *reader) {
    uint8_t minor = client->request_minor;
    if (minor == 0) { /* X_RenderQueryVersion */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply_begin(client, reply, 0U, 0U);
        reply_put_u32(client, reply, 8, 0U);  /* major version */
        reply_put_u32(client, reply, 12, 11U); /* minor version */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 1) { /* X_RenderQueryPictFormats */
        render_query_pict_formats(server, client);
        return;
    }
    if (minor == 2) { /* X_RenderQueryPictIndexValues */
        (void)rd_u32(client, reader);
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply_begin(client, reply, 0U, 0U);
        reply_put_u32(client, reply, 8, 0U); /* numIndexValues */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 4) { /* X_RenderCreatePicture */
        uint32_t id = rd_u32(client, reader);
        uint32_t drawable = rd_u32(client, reader);
        uint32_t format = rd_u32(client, reader);
        uint32_t value_mask = rd_u32(client, reader);
        if (!drawable_exists(server, drawable)) {
            x_reply_error(client, BadDrawable, 0, drawable);
            return;
        }
        if (x_picture_of(server, id)) {
            x_reply_error(client, BadIDChoice, 0, id);
            return;
        }
        struct x_picture *picture = x_picture_create(server, client, id);
        if (!picture) {
            x_reply_error(client, BadAlloc, 0, 0U);
            return;
        }
        picture->drawable = drawable;
        uint16_t width = 0U;
        uint16_t height = 0U;
        int is_window = 0;
        (void)x_drawable_pixels(server, drawable, &width, &height, &is_window);
        picture->width = width;
        picture->height = height;
        picture->format = format;
        render_read_picture_values(client, reader, value_mask, picture);
        return;
    }
    if (minor == 5) { /* X_RenderChangePicture */
        uint32_t id = rd_u32(client, reader);
        uint32_t value_mask = rd_u32(client, reader);
        struct x_picture *picture = x_picture_of(server, id);
        if (!picture) {
            x_reply_error(client, BadPicture, 0, id);
            return;
        }
        render_read_picture_values(client, reader, value_mask, picture);
        return;
    }
    if (minor == 6) { /* X_RenderSetPictureClipRectangles */
        uint32_t id = rd_u32(client, reader);
        (void)rd_i16(client, reader);
        (void)rd_i16(client, reader);
        struct x_picture *picture = x_picture_of(server, id);
        if (!picture) {
            x_reply_error(client, BadPicture, 0, id);
            return;
        }
        return;
    }
    if (minor == 7) { /* X_RenderFreePicture */
        uint32_t id = rd_u32(client, reader);
        if (!x_picture_of(server, id)) {
            x_reply_error(client, BadPicture, 0, id);
            return;
        }
        x_picture_free(server, id);
        return;
    }
    if (minor == 8) { /* X_RenderComposite */
        render_composite(server, client, reader);
        return;
    }
    if (minor == 26) { /* X_RenderFillRectangles */
        uint8_t operator_code = rd_u8(reader);
        (void)x_reader_skip(reader, 3U);
        uint32_t destination_id = rd_u32(client, reader);
        uint16_t red = rd_u16(client, reader);
        uint16_t green = rd_u16(client, reader);
        uint16_t blue = rd_u16(client, reader);
        uint16_t alpha = rd_u16(client, reader);
        struct x_picture *picture = x_picture_of(server, destination_id);
        if (!picture) {
            x_reply_error(client, BadPicture, 0, destination_id);
            return;
        }
        uint32_t pixel = 0U;
        x_rgb_to_pixel(red, green, blue, &pixel);
        pixel |= ((uint32_t)(alpha >> 8)) << 24;

        /* The rectangles follow the fixed part of the request. */
        while (x_reader_has(reader, 8U)) {
            int16_t x = rd_i16(client, reader);
            int16_t y = rd_i16(client, reader);
            uint16_t width = rd_u16(client, reader);
            uint16_t height = rd_u16(client, reader);
            if (operator_code == X_PICT_OP_SRC || operator_code == X_PICT_OP_OVER) {
                x_drawable_fill(server, picture->drawable, x, y, width, height,
                                pixel);
            }
        }
        return;
    }
    if (minor == 27) { /* X_RenderCreateCursor */
        uint32_t id = rd_u32(client, reader);
        uint32_t source_id = rd_u32(client, reader);
        uint16_t x = rd_u16(client, reader);
        uint16_t y = rd_u16(client, reader);
        (void)x;
        (void)y;
        struct x_picture *source = x_picture_of(server, source_id);
        if (!source) {
            x_reply_error(client, BadPicture, 0, source_id);
            return;
        }
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply_begin(client, reply, 0U, 0U);
        reply_put_u32(client, reply, 8, id);
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 29) { /* X_RenderQueryFilters */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply_begin(client, reply, 0U, 0U);
        reply_put_u32(client, reply, 8, 0U); /* numFilters */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    /* Unknown or unsupported minor request */
    x_reply_error(client, BadRequest, client->request_major, 0U);
}

/* ------------------------------------------------------------------- XFIXES */

/* The cursor names that the clients of the server set. */
static char x_cursor_names[X_MAX_CURSOR_NAMES][64];
static uint32_t x_cursor_name_atoms[X_MAX_CURSOR_NAMES];

static void dispatch_fixes_request(struct x_server *server, struct x_client *client,
                                   struct x_reader *reader) {
    uint8_t minor = client->request_minor;
    if (minor == 0) { /* X_XFixesQueryVersion */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply_begin(client, reply, 0U, 0U);
        reply_put_u32(client, reply, 8, 6U); /* major version */
        reply_put_u32(client, reply, 12, 0U); /* minor version */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 1) { /* X_XFixesSelectCursorInput */
        (void)rd_u32(client, reader);
        (void)rd_u32(client, reader);
        return;
    }
    if (minor == 2) { /* X_XFixesGetCursorImage */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply_begin(client, reply, 0U, 0U);
        reply_put_u16(client, reply, 8, (uint16_t)server->pointer_x);
        reply_put_u16(client, reply, 10, (uint16_t)server->pointer_y);
        reply_put_u16(client, reply, 12, 1U);  /* width */
        reply_put_u16(client, reply, 14, 1U);  /* height */
        reply_put_u16(client, reply, 16, 0U);  /* xhot */
        reply_put_u16(client, reply, 18, 0U);  /* yhot */
        reply_put_u32(client, reply, 20, 0U);  /* the cursor serial */
        x_reply_bytes(client, reply, sizeof(reply));
        uint32_t pixel = 0U;
        x_reply_bytes(client, &pixel, sizeof(pixel));
        return;
    }
    if (minor == 4) { /* X_XFixesExpandMacros */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply_begin(client, reply, 0U, 0U);
        reply_put_u32(client, reply, 8, 0U); /* numSteps */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 9) { /* X_XFixesSetCursorName */
        uint32_t cursor = rd_u32(client, reader);
        uint8_t length = rd_u8(reader);
        const uint8_t *name = x_reader_bytes(reader, length);
        if (name) {
            for (int slot = 0; slot < X_MAX_CURSOR_NAMES; ++slot) {
                if (x_cursor_name_atoms[slot] != 0U &&
                    x_cursor_name_atoms[slot] != cursor) {
                    continue;
                }
                size_t stored = length;
                if (stored >= sizeof(x_cursor_names[0])) {
                    stored = sizeof(x_cursor_names[0]) - 1U;
                }
                memcpy(x_cursor_names[slot], name, stored);
                x_cursor_names[slot][stored] = '\0';
                x_cursor_name_atoms[slot] = cursor;
                break;
            }
        }
        return;
    }
    if (minor == 10) { /* X_XFixesGetCursorName */
        uint32_t cursor = rd_u32(client, reader);
        const char *name = "";
        for (int slot = 0; slot < X_MAX_CURSOR_NAMES; ++slot) {
            if (x_cursor_name_atoms[slot] == cursor) name = x_cursor_names[slot];
        }
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        size_t length = strlen(name);
        size_t padded = (length + 3U) & ~3U;
        reply_begin(client, reply, 0U, (uint32_t)(padded / 4U));
        reply_put_u32(client, reply, 8, x_atom_intern(server, name, length, 0));
        x_reply_bytes(client, reply, sizeof(reply));
        if (padded) {
            uint8_t buffer[256];
            memset(buffer, 0, sizeof(buffer));
            if (padded <= sizeof(buffer)) {
                memcpy(buffer, name, length);
                x_reply_bytes(client, buffer, padded);
            }
        }
        return;
    }
    /* The region requests of the extension carry the shapes of the windows,
     * and the server keeps the shapes in the shape extension. */
    if (minor == 5 || minor == 6 || minor == 7 || minor == 8) {
        (void)rd_u32(client, reader);
        return;
    }
    /* Unknown or unsupported minor request */
    x_reply_error(client, BadRequest, client->request_major, 0U);
}

/* ----------------------------------------------------------------- X-DAMAGE */

/* Find a damage report of the server by its identifier. */
static struct x_damage *x_damage_of(struct x_server *server, xid_t id) {
    if (id == 0U) return NULL;
    for (int slot = 0; slot < X_MAX_DAMAGES; ++slot) {
        if (server->damages[slot].id == id) return &server->damages[slot];
    }
    return NULL;
}

/* Tell the clients that asked for reports that a part of a drawable changed.
 *
 * The event carries the drawable, the report and the rectangle of the
 * change. */
void x_server_report_damage(struct x_server *server, xid_t drawable, int16_t x,
                             int16_t y, uint16_t width, uint16_t height) {
    for (int slot = 0; slot < X_MAX_DAMAGES; ++slot) {
        struct x_damage *damage = &server->damages[slot];
        if (damage->id == 0U) continue;
        if (damage->drawable != drawable) continue;
        if (damage->client < 0 || damage->client >= X_MAX_CLIENTS) continue;
        struct x_client *client = &server->clients[damage->client];
        if (!client->in_use) continue;

        uint8_t event[20];
        memset(event, 0, sizeof(event));
        event[0] = X_EXT_DAMAGE_EVENT;
        reply_put_u16(client, event, 2, (uint16_t)client->sequence);
        reply_put_u32(client, event, 4, 0U);
        reply_put_u32(client, event, 8, drawable);
        reply_put_u32(client, event, 12, damage->id);
        reply_put_u16(client, event, 16, (uint16_t)x);
        reply_put_u16(client, event, 18, (uint16_t)y);
        /* The width and the height of the change follow in the next word. */
        uint8_t extended[24];
        memset(extended, 0, sizeof(extended));
        memcpy(extended, event, 20);
        reply_put_u16(client, extended, 20, width);
        reply_put_u16(client, extended, 22, height);
        x_event_queue(client, extended, sizeof(extended), drawable);
    }
}

static void dispatch_damage_request(struct x_server *server, struct x_client *client,
                                    struct x_reader *reader) {
    uint8_t minor = client->request_minor;
    if (minor == 0) { /* X_XDamageQueryVersion */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply_begin(client, reply, 0U, 0U);
        reply_put_u32(client, reply, 8, 1U); /* major version */
        reply_put_u32(client, reply, 12, 1U); /* minor version */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 1) { /* X_XDamageCreate */
        uint32_t drawable = rd_u32(client, reader);
        uint8_t level = rd_u8(reader);
        (void)x_reader_skip(reader, 3U);
        if (!drawable_exists(server, drawable)) {
            x_reply_error(client, BadDrawable, 0, drawable);
            return;
        }
        uint32_t id = 0U;
        for (int slot = 0; slot < X_MAX_DAMAGES; ++slot) {
            if (server->damages[slot].id == 0U) {
                id = (uint32_t)(X_MAX_DAMAGES + slot + 1U);
                break;
            }
        }
        if (id == 0U) {
            x_reply_error(client, BadAlloc, 0, 0U);
            return;
        }
        struct x_damage *damage = &server->damages[id - X_MAX_DAMAGES - 1U];
        memset(damage, 0, sizeof(*damage));
        damage->id = id;
        damage->client = client_index_of(server, client);
        damage->drawable = drawable;
        damage->level = (uint8_t)(level & 0xffU);

        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply_begin(client, reply, 0U, 0U);
        reply_put_u32(client, reply, 8, id);
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 2) { /* X_XDamageDestroy */
        uint32_t id = rd_u32(client, reader);
        struct x_damage *damage = x_damage_of(server, id);
        if (!damage) {
            x_reply_error(client, BadDrawable, 0, id);
            return;
        }
        memset(damage, 0, sizeof(*damage));
        return;
    }
    if (minor == 3 || minor == 4) { /* X_XDamageSubtract and Add */
        uint32_t id = rd_u32(client, reader);
        if (!x_damage_of(server, id)) {
            x_reply_error(client, BadDrawable, 0, id);
            return;
        }
        (void)rd_i16(client, reader);
        (void)rd_i16(client, reader);
        (void)rd_u16(client, reader);
        (void)rd_u16(client, reader);
        return;
    }
    /* Unknown or unsupported minor request */
    x_reply_error(client, BadRequest, client->request_major, 0U);
}

#endif
