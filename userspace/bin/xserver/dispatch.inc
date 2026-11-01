/* The request dispatcher of the akaOS X11 display server.
 *
 * The file is included by the unit that holds the connection handling, so the
 * helpers of the protocol live next to the dispatcher that uses them.
 *
 * Every request is decoded through the bounds checked reader of the protocol
 * header, and every reply is built with the upstream reply structures, so that
 * the server and a real client agree on every field offset.
 */
#ifndef AKAOS_XSERVER_DISPATCH_INC
#define AKAOS_XSERVER_DISPATCH_INC

/* --------------------------------------------------------------- byte order */
static uint16_t rd_u16(struct x_client *client, struct x_reader *reader) {
    return client_u16(client, x_reader_u16(reader));
}

static uint32_t rd_u32(struct x_client *client, struct x_reader *reader) {
    return client_u32(client, x_reader_u32(reader));
}

static int16_t rd_i16(struct x_client *client, struct x_reader *reader) {
    return (int16_t)rd_u16(client, reader);
}

static uint8_t rd_u8(struct x_reader *reader) {
    return x_reader_u8(reader);
}

/* Resolve a window identifier to a window index. */
static int resolve_window(struct x_server *server, uint32_t id) {
    return x_window_index(server, id);
}

/* Whether a drawable of that identifier exists.
 *
 * A drawable is either a window or a pixmap, and a request that draws into
 * one has to accept both. */
static int drawable_exists(struct x_server *server, uint32_t id) {
    if (x_window_index(server, id) >= 0) return 1;
    return x_pixmap_exists(server, id);
}

/* -------------------------------------------------------------- value lists */
/* Read a one byte value of a window attribute list.
 *
 * Every value of the list is aligned to four bytes, so a value that is one
 * byte wide is followed by three bytes of padding. */
static uint8_t rd_window_flag(struct x_reader *reader) {
    uint8_t value = rd_u8(reader);
    (void)x_reader_skip(reader, 3U);
    return value;
}

/* Read the value list of a window.
 *
 * The values appear in the order of the bits of the mask, and every one of
 * them is aligned to four bytes.  The list is the same for a window that is
 * created and for a window whose attributes change. */
static void read_window_values(struct x_server *server, struct x_client *client,
                               struct x_reader *reader, uint32_t value_mask,
                               int index) {
    struct x_window *window = &server->windows[index];
    if (value_mask & (1U << 0)) (void)rd_u32(client, reader);   /* background-pixmap */
    if (value_mask & (1U << 1)) window->background_pixel = rd_u32(client, reader);
    if (value_mask & (1U << 2)) (void)rd_u32(client, reader);   /* border-pixmap */
    if (value_mask & (1U << 3)) window->border_pixel = rd_u32(client, reader);
    if (value_mask & (1U << 4)) (void)rd_window_flag(reader);   /* bit-gravity */
    if (value_mask & (1U << 5)) (void)rd_window_flag(reader);   /* win-gravity */
    if (value_mask & (1U << 6)) (void)rd_window_flag(reader);   /* backing-store */
    if (value_mask & (1U << 7)) (void)rd_u32(client, reader);   /* backing-planes */
    if (value_mask & (1U << 8)) (void)rd_u32(client, reader);   /* backing-pixel */
    if (value_mask & (1U << 9)) window->override_redirect = rd_window_flag(reader) ? 1U : 0U;
    if (value_mask & (1U << 10)) (void)rd_window_flag(reader);  /* save-under */
    if (value_mask & (1U << 11)) {                             /* event-mask */
        x_window_select_events(server, index, client_index_of(server, client),
                               rd_u32(client, reader));
    }
    if (value_mask & (1U << 12)) (void)rd_u32(client, reader);  /* do-not-propagate */
    if (value_mask & (1U << 13)) window->colormap = rd_u32(client, reader);
    if (value_mask & (1U << 14)) window->cursor = rd_u32(client, reader);
}

/* Read a graphics context value list.
 *
 * The values appear in ascending bit order and every one of them is aligned
 * to four bytes, so a value that is narrower than that is followed by
 * padding. */
static uint8_t rd_gc_flag(struct x_reader *reader) {
    uint8_t value = rd_u8(reader);
    (void)x_reader_skip(reader, 3U);
    return value;
}

static uint16_t rd_gc_short(struct x_reader *reader) {
    uint16_t value = (uint16_t)x_reader_u16(reader);
    (void)x_reader_skip(reader, 2U);
    return value;
}

static void read_gc_values(struct x_client *client, struct x_reader *reader,
                           uint32_t value_mask, struct x_gc *gc) {
    if (value_mask & (1U << 0)) gc->function = rd_u32(client, reader);
    if (value_mask & (1U << 1)) gc->plane_mask = rd_u32(client, reader);
    if (value_mask & (1U << 2)) gc->foreground = rd_u32(client, reader);
    if (value_mask & (1U << 3)) gc->background = rd_u32(client, reader);
    if (value_mask & (1U << 4)) gc->line_width = rd_gc_short(reader);
    if (value_mask & (1U << 5)) (void)rd_gc_flag(reader);   /* line style */
    if (value_mask & (1U << 6)) (void)rd_gc_flag(reader);   /* cap style */
    if (value_mask & (1U << 7)) (void)rd_gc_flag(reader);   /* join style */
    if (value_mask & (1U << 8)) gc->fill_style = rd_gc_flag(reader);
    if (value_mask & (1U << 9)) (void)rd_gc_flag(reader);   /* fill rule */
    if (value_mask & (1U << 10)) (void)rd_u32(client, reader);  /* tile */
    if (value_mask & (1U << 11)) (void)rd_u32(client, reader);  /* stipple */
    if (value_mask & (1U << 12)) (void)rd_gc_short(reader);
    if (value_mask & (1U << 13)) (void)rd_gc_short(reader);
    if (value_mask & (1U << 14)) gc->font = rd_u32(client, reader);
    if (value_mask & (1U << 15)) (void)rd_gc_flag(reader);  /* subwindow mode */
    if (value_mask & (1U << 16)) gc->graphics_exposures = rd_gc_flag(reader);
    if (value_mask & (1U << 17)) gc->clip_x = (int16_t)rd_gc_short(reader);
    if (value_mask & (1U << 18)) gc->clip_y = (int16_t)rd_gc_short(reader);
    if (value_mask & (1U << 19)) (void)rd_u32(client, reader);  /* clip mask */
    if (value_mask & (1U << 20)) gc->dash_offset = rd_gc_short(reader);
    if (value_mask & (1U << 21)) gc->dashes = rd_u32(client, reader);
    if (value_mask & (1U << 22)) (void)rd_gc_flag(reader);  /* arc mode */
}

/* ------------------------------------------------------------------ replies */
static void reply_put_u16(struct x_client *client, uint8_t *buffer, size_t offset,
                          uint16_t value) {
    uint16_t swapped = client_u16(client, value);
    memcpy(buffer + offset, &swapped, 2);
}

static void reply_put_u32(struct x_client *client, uint8_t *buffer, size_t offset,
                          uint32_t value) {
    uint32_t swapped = client_u32(client, value);
    memcpy(buffer + offset, &swapped, 4);
}

/* Build the thirty two byte header of a reply.
 *
 * The length field counts the additional data of the reply in four byte
 * units. */
static void reply_begin(struct x_client *client, uint8_t *buffer, uint8_t detail,
                        uint32_t length_units) {
    memset(buffer, 0, 32);
    buffer[0] = X_Reply;
    buffer[1] = detail;
    reply_put_u16(client, buffer, 2, (uint16_t)client->sequence);
    reply_put_u32(client, buffer, 4, length_units);
}

/* --------------------------------------------------------------- text items */
/* The bounds of the text item list of a PolyText request. */
#define X_MAX_TEXT_ITEMS 128U
#define X_MAX_TEXT_BYTES 8192U

/* Parse the text item list of a PolyText request.
 *
 * A PolyText8 item and a PolyText16 item share the same layout: the number of
 * characters, the offset from the position of the previous item and then the
 * characters themselves.  The characters of a PolyText16 item are sixteen bit
 * wide, so they are converted into eight bit ones, because the server draws
 * them with the same cell font.
 *
 * A character count of 255 marks a font change, which carries a four byte
 * font identifier instead of characters. */
static size_t parse_text_items(struct x_reader *reader, int sixteen_bit,
                               struct x_text_item *items, size_t capacity,
                               uint8_t *storage, size_t storage_size) {
    size_t count = 0U;
    size_t stored = 0U;
    for (;;) {
        if (!x_reader_has(reader, 2U)) break;
        uint8_t characters = rd_u8(reader);
        int8_t delta = (int8_t)rd_u8(reader);
        if (characters == 255U) {
            /* A font change carries the four byte font identifier. */
            if (!x_reader_has(reader, 4U)) break;
            x_reader_skip(reader, 4U);
            continue;
        }
        size_t bytes = (size_t)characters * (sixteen_bit ? 2U : 1U);
        const uint8_t *raw = x_reader_bytes(reader, bytes);
        if (!raw) break;
        if (count < capacity) {
            const uint8_t *text = raw;
            if (sixteen_bit) {
                if (stored + characters > storage_size) break;
                for (uint32_t index = 0U; index < characters; ++index) {
                    storage[stored + index] = raw[index * 2U];
                }
                text = storage + stored;
                stored += characters;
            }
            items[count].delta_x = delta;
            items[count].delta_y = 0;
            items[count].text = text;
            items[count].length = characters;
        }
        ++count;
        if (count >= capacity) break;
    }
    return count;
}

/* -------------------------------------------------------------- window tree */
static void request_create_window(struct x_server *server,
                                  struct x_client *client,
                                  struct x_reader *reader) {
    /* The request header carries the depth in its data byte, so the body
     * starts directly at the window identifier. */
    (void)client->request_minor;
    uint32_t id = rd_u32(client, reader);
    uint32_t parent_id = rd_u32(client, reader);
    int16_t x = rd_i16(client, reader);
    int16_t y = rd_i16(client, reader);
    uint16_t width = rd_u16(client, reader);
    uint16_t height = rd_u16(client, reader);
    uint16_t border = rd_u16(client, reader);
    uint16_t input_class = rd_u16(client, reader);
    uint32_t visual = rd_u32(client, reader);
    uint32_t value_mask = rd_u32(client, reader);

    int parent_index = resolve_window(server, parent_id);
    if (parent_index < 0) {
        x_reply_error(client, BadWindow, 0, parent_id);
        return;
    }
    if (id == 0U || x_window_index(server, id) >= 0) {
        x_reply_error(client, BadIDChoice, 0, id);
        return;
    }

    int index = x_window_create(server, client, id, parent_index, x, y, width,
                                 height, border, input_class, visual);
    if (index < 0) {
        x_reply_error(client, BadAlloc, 0, 0U);
        return;
    }
    struct x_window *window = &server->windows[index];

    read_window_values(server, client, reader, value_mask, index);

    window->all_event_masks = x_window_all_event_masks(server, index);
    /* A new window starts out with its background painted into the backing
     * store so the screen shows the background colour and not black. */
    x_window_fill_backing(server, index, window->background_pixel);
    x_event_create(server, index);
}

static void request_change_window_attributes(struct x_server *server,
                                             struct x_client *client,
                                             struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint32_t value_mask = rd_u32(client, reader);
    int index = resolve_window(server, id);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, id);
        return;
    }
    struct x_window *window = &server->windows[index];

    read_window_values(server, client, reader, value_mask, index);

    window->all_event_masks = x_window_all_event_masks(server, index);
    if (window->parent < 0) {
        /* The root window paints its background into its own backing store. */
        x_window_fill_backing(server, index, window->background_pixel);
    }
    x_server_repaint(server);
}

static uint8_t window_map_state(struct x_server *server, int index) {
    if (index < 0 || index >= X_MAX_WINDOWS || !server->windows[index].mapped)
        return 0U; /* IsUnmapped */
    int current = server->windows[index].parent;
    int depth = 0;
    while (current >= 0 && current < X_MAX_WINDOWS && depth < X_MAX_WINDOWS) {
        if (!server->windows[current].mapped) return 1U; /* IsUnviewable */
        current = server->windows[current].parent;
        ++depth;
    }
    return 2U; /* IsViewable */
}

static void request_get_window_attributes(struct x_server *server,
                                          struct x_client *client,
                                          struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    int index = resolve_window(server, id);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, id);
        return;
    }
    struct x_window *window = &server->windows[index];
    xGetWindowAttributesReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)((sizeof(reply) - 32U) / 4U);
    reply.visualID = (VisualID)client_u32(client, window->visual_id);
    reply.class = (CARD16)client_u16(client, (uint16_t)window->input_class);
    reply.bitGravity = 0U;
    reply.winGravity = 0U;
    reply.backingBitPlanes = (CARD32)client_u32(client, 0xffffffffU);
    reply.backingPixel = (CARD32)client_u32(client, window->background_pixel);
    reply.saveUnder = 0U;
    reply.mapInstalled = 1U;
    reply.mapState = (CARD8)client_u8(client, window_map_state(server, index));
    reply.override = (BOOL)client_u8(client, window->override_redirect ? 1U : 0U);
    reply.colormap = (Colormap)client_u32(client, window->colormap);
    reply.allEventMasks = (CARD32)client_u32(client, window->all_event_masks);
    reply.yourEventMask = (CARD32)client_u32(client,
                                     x_window_event_mask(server, index,
                                                         client_index_of(server, client)));
    reply.doNotPropagateMask = 0U;
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_get_geometry(struct x_server *server, struct x_client *client,
                                 struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    int index = resolve_window(server, id);
    if (index < 0) {
        x_reply_error(client, BadDrawable, 0, id);
        return;
    }
    struct x_window *window = &server->windows[index];
    xGetGeometryReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.depth = (CARD8)client_u8(client, 24U);
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    reply.root = (Window)client_u32(client, server->windows[0].id);
    reply.x = (INT16)client_u16(client, (uint16_t)window->x);
    reply.y = (INT16)client_u16(client, (uint16_t)window->y);
    reply.width = (CARD16)client_u16(client, window->width);
    reply.height = (CARD16)client_u16(client, window->height);
    reply.borderWidth = (CARD16)client_u16(client, window->border_width);
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_query_tree(struct x_server *server, struct x_client *client,
                               struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    int index = resolve_window(server, id);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, id);
        return;
    }
    struct x_window *window = &server->windows[index];

    int children[X_MAX_WINDOWS];
    int count = 0;
    for (int child = window->first_child; child >= 0;
         child = server->windows[child].next_sibling) {
        if (count >= X_MAX_WINDOWS) break;
        children[count++] = child;
    }

    uint8_t buffer[32];
    /* The reply carries the root of the screen, the parent of the window and
     * then the number of children.  The parent is the window the tree walk
     * finds, not the window itself, or a client that checks where a window
     * manager put it reads its own identifier back. */
    uint32_t parent_id = window->parent >= 0
                             ? server->windows[window->parent].id : 0U;
    reply_begin(client, buffer, 0U, (uint32_t)count);
    reply_put_u32(client, buffer, 8, (uint32_t)client_u32(client,
                                  server->windows[0].id));
    reply_put_u32(client, buffer, 12, (uint32_t)client_u32(client, parent_id));
    reply_put_u16(client, buffer, 16, (uint16_t)client_u16(client, (uint16_t)count));
    x_reply_bytes(client, buffer, sizeof(buffer));
    for (int child = 0; child < count; ++child) {
        uint8_t entry[4];
        reply_put_u32(client, entry, 0, (uint32_t)client_u32(client,
                                  server->windows[children[child]].id));
        x_reply_bytes(client, entry, sizeof(entry));
    }
}

static void request_map_window(struct x_server *server, struct x_client *client,
                                struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    int index = resolve_window(server, id);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, id);
        return;
    }
    /* A window manager that redirected the parent decides when the window
     * becomes visible, so the request goes to it instead of being applied. */
    if (x_event_map_request(server, index, client_index_of(server, client))) {
        x_server_repaint(server);
        return;
    }
    x_window_map(server, index);
    x_server_repaint(server);
}

static void request_unmap_window(struct x_server *server, struct x_client *client,
                                  struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    int index = resolve_window(server, id);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, id);
        return;
    }
    x_window_unmap(server, index);
    x_server_repaint(server);
}

static void request_configure_window(struct x_server *server,
                                     struct x_client *client,
                                     struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint16_t value_mask = rd_u16(client, reader);
    /* The value mask is followed by two bytes of padding, and the values
     * start after them. */
    (void)x_reader_skip(reader, 2U);
    int index = resolve_window(server, id);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, id);
        return;
    }
    /* The values the request does not mention keep what the window has now.
     * A window manager moves and restacks a window without resizing it, and a
     * configure request that carried zeros instead of the current size
     * destroyed the backing store of the window. */
    struct x_window *target = &server->windows[index];
    int16_t x = target->x;
    int16_t y = target->y;
    uint16_t width = target->width;
    uint16_t height = target->height;
    uint16_t border_width = target->border_width;
    uint32_t sibling = 0;
    /* A stack mode of zero is a real request, so an absent one needs its own
     * value that the window layer reads as "leave the stack alone". */
    uint16_t stack_mode = 0xffffU;
    /* Every value of the list of a window configuration is a full word, and
     * the width and the height of the window are two such words.  Reading
     * them as narrow values takes the height out of the upper half of the
     * width, and a window that a client asks to resize keeps the size it was
     * created with. */
    if (value_mask & (1U << 0)) x = (int16_t)(uint16_t)rd_u32(client, reader);
    if (value_mask & (1U << 1)) y = (int16_t)(uint16_t)rd_u32(client, reader);
    if (value_mask & (1U << 2)) width = (uint16_t)rd_u32(client, reader);
    if (value_mask & (1U << 3)) height = (uint16_t)rd_u32(client, reader);
    if (value_mask & (1U << 4)) border_width = (uint16_t)rd_u32(client, reader);
    if (value_mask & (1U << 5)) sibling = rd_u32(client, reader);
    if (value_mask & (1U << 6)) stack_mode = (uint16_t)rd_u32(client, reader);

    /* A window manager that redirected the parent decides when and how the
     * window is configured. */
    if (x_event_configure_request(server, client, index, value_mask, x, y, width,
                                   height, border_width, sibling, stack_mode)) {
        x_server_repaint(server);
        return;
    }
    x_window_configure(server, index, x, y, width, height, border_width, sibling,
                       stack_mode);
    x_server_repaint(server);
}

static void request_destroy_window(struct x_server *server, struct x_client *client,
                                   struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    int index = resolve_window(server, id);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, id);
        return;
    }
    x_window_destroy(server, index);
    x_server_repaint(server);
}

static void request_reparent_window(struct x_server *server,
                                     struct x_client *client,
                                     struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint32_t parent_id = rd_u32(client, reader);
    int16_t x = rd_i16(client, reader);
    int16_t y = rd_i16(client, reader);
    int index = resolve_window(server, id);
    int parent_index = resolve_window(server, parent_id);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, id);
        return;
    }
    if (parent_index < 0) {
        x_reply_error(client, BadWindow, 0, parent_id);
        return;
    }
    x_window_reparent(server, index, parent_index, x, y);
    x_server_repaint(server);
}

/* ---------------------------------------------------------------- resources */
static struct x_gc *find_gc(struct x_server *server, uint32_t id) {
    if (id == 0U) return NULL;
    for (int index = 0; index < X_MAX_GCS; ++index) {
        if (server->gcs[index].id == id) return &server->gcs[index];
    }
    return NULL;
}

/* ------------------------------------------------------------------ atoms */
static void request_intern_atom(struct x_server *server, struct x_client *client,
                                struct x_reader *reader) {
    uint16_t name_length = rd_u16(client, reader);
    (void)x_reader_skip(reader, 2U);
    int only_if_exists = client->request_minor ? 1 : 0;
    const uint8_t *name = x_reader_bytes(reader, name_length);
    if (!name) {
        x_reply_error(client, BadLength, 0, 0U);
        return;
    }
    uint32_t atom = x_atom_intern(server, (const char *)name, name_length,
                                  only_if_exists);
    xInternAtomReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    reply.atom = (Atom)client_u32(client, atom);
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_get_atom_name(struct x_server *server, struct x_client *client,
                                  struct x_reader *reader) {
    uint32_t atom = rd_u32(client, reader);
    size_t length = 0U;
    const char *name = x_atom_name(server, atom, &length);
    if (!name) {
        x_reply_error(client, BadAtom, 0, atom);
        return;
    }
    xGetAtomNameReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)((length + 3U) / 4U);
    reply.nameLength = (CARD16)client_u16(client, (uint16_t)length);
    x_reply_bytes(client, &reply, sizeof(reply));
    uint8_t padded[256];
    memset(padded, 0, sizeof(padded));
    size_t copy = length < sizeof(padded) ? length : sizeof(padded);
    memcpy(padded, name, copy);
    x_reply_bytes(client, padded, (length + 3U) / 4U * 4U);
}

/* -------------------------------------------------------------- properties */
static void request_change_property(struct x_server *server,
                                     struct x_client *client,
                                     struct x_reader *reader) {
    uint32_t window = rd_u32(client, reader);
    uint32_t property = rd_u32(client, reader);
    uint32_t type = rd_u32(client, reader);
    uint8_t format = rd_u8(reader);
    (void)x_reader_skip(reader, 3U);
    uint32_t item_count = rd_u32(client, reader);
    uint32_t unit = (format == 32U) ? 4U : ((format == 16U) ? 2U : 1U);
    size_t bytes = (size_t)item_count * unit;
    const uint8_t *data = x_reader_bytes(reader, bytes);
    if (!data) {
        x_reply_error(client, BadLength, 0, item_count);
        return;
    }
    int index = resolve_window(server, window);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, window);
        return;
    }
    (void)x_property_set(server, server->windows[index].id, property, type, format,
                          data, item_count);
    x_event_property(server, index, property, 0U);
}

static void request_delete_property(struct x_server *server,
                                    struct x_client *client,
                                    struct x_reader *reader) {
    uint32_t window = rd_u32(client, reader);
    uint32_t property = rd_u32(client, reader);
    int index = resolve_window(server, window);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, window);
        return;
    }
    (void)x_property_delete(server, server->windows[index].id, property);
    x_event_property(server, index, property, 1U);
}

static void request_get_property(struct x_server *server, struct x_client *client,
                                 struct x_reader *reader) {
    uint32_t window = rd_u32(client, reader);
    uint32_t property = rd_u32(client, reader);
    uint32_t type = rd_u32(client, reader);
    uint32_t long_offset = rd_u32(client, reader);
    uint32_t long_length = rd_u32(client, reader);
    int delete_after = client->request_minor ? 1 : 0;
    /* The type of the request is a hint, and the server answers with the type
     * the property really has. */
    (void)type;
    int index = resolve_window(server, window);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, window);
        return;
    }
    uint32_t found_type = 0U;
    uint8_t format = 0U;
    uint32_t item_count = 0U;
    const uint8_t *data = NULL;
    if (!x_property_get(server, server->windows[index].id, property, &found_type,
                        &format, &item_count, &data)) {
        /* A property that does not exist is not an error.  The reply carries
         * no type and no value. */
        found_type = 0U;
        format = 0U;
        item_count = 0U;
        data = NULL;
    }
    uint32_t unit = (format == 32U) ? 4U : ((format == 16U) ? 2U : 1U);
    uint32_t total_items = item_count;
    if (long_offset >= total_items) {
        item_count = 0U;
        data = NULL;
    } else {
        uint32_t available = total_items - long_offset;
        uint32_t wanted = (long_length == 0U) ? available : long_length;
        if (wanted > available) wanted = available;
        item_count = wanted;
        data = data ? data + (size_t)long_offset * unit : NULL;
    }
    size_t bytes = (size_t)item_count * unit;
    size_t padded = (bytes + 3U) / 4U * 4U;

    xGetPropertyReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.format = format;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)(padded / 4U);
    reply.propertyType = (Atom)client_u32(client, found_type);
    reply.bytesAfter = (CARD32)client_u32(client, 0U);
    reply.nItems = (CARD32)client_u32(client, item_count);
    x_reply_bytes(client, &reply, sizeof(reply));
    if (padded > 0U) {
        uint8_t buffer[256];
        size_t offset = 0U;
        while (offset < padded) {
            size_t chunk = padded - offset;
            if (chunk > sizeof(buffer)) chunk = sizeof(buffer);
            memset(buffer, 0, chunk);
            if (data && offset < bytes) {
                size_t copy = bytes - offset;
                if (copy > chunk) copy = chunk;
                memcpy(buffer, data + offset, copy);
            }
            x_reply_bytes(client, buffer, chunk);
            offset += chunk;
        }
    }
    if (delete_after) {
        (void)x_property_delete(server, server->windows[index].id, property);
        x_event_property(server, index, property, 1U);
    }
}

static void request_list_properties(struct x_server *server,
                                    struct x_client *client,
                                    struct x_reader *reader) {
    uint32_t window = rd_u32(client, reader);
    int index = resolve_window(server, window);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, window);
        return;
    }
    uint32_t atoms[X_MAX_PROPERTIES];
    int count = x_property_list(server, server->windows[index].id, atoms,
                                X_MAX_PROPERTIES);
    xListPropertiesReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)count;
    reply.nProperties = (CARD16)client_u16(client, (uint16_t)count);
    x_reply_bytes(client, &reply, sizeof(reply));
    for (int entry = 0; entry < count; ++entry) {
        uint8_t buffer[4];
        reply_put_u32(client, buffer, 0, (uint32_t)client_u32(client, atoms[entry]));
        x_reply_bytes(client, buffer, sizeof(buffer));
    }
}

/* -------------------------------------------------------------- selections */
static void request_set_selection_owner(struct x_server *server,
                                         struct x_client *client,
                                         struct x_reader *reader) {
    uint32_t owner = rd_u32(client, reader);
    uint32_t selection = rd_u32(client, reader);
    uint32_t time = rd_u32(client, reader);
    for (int index = 0; index < X_MAX_SELECTIONS; ++index) {
        if (server->selections[index].selection == selection) {
            uint32_t previous_owner = server->selections[index].owner;
            server->selections[index].owner = owner;
            if (previous_owner != 0U && previous_owner != owner) {
                int prev_index = x_window_index(server, previous_owner);
                int prev_client = prev_index >= 0 ? server->windows[prev_index].client : -1;
                if (prev_client >= 0 && prev_client != client_index_of(server, client)) {
                    struct x_client *target = &server->clients[prev_client];
                    uint32_t stamp = time != 0U ? time : (uint32_t)(server_now_millis() & 0xffffffffU);
                    uint8_t event[32];
                    memset(event, 0, sizeof(event));
                    event[0] = SelectionClear;
                    memcpy(event + 2, &(CARD16){client_u16(target, (uint16_t)target->sequence)}, 2);
                    memcpy(event + 4, &(CARD32){client_u32(target, stamp)}, 4);
                    memcpy(event + 8, &(CARD32){client_u32(target, previous_owner)}, 4);
                    memcpy(event + 12, &(CARD32){client_u32(target, selection)}, 4);
                    x_event_queue(target, event, sizeof(event), 0U);
                }
            }
            /* The request has no reply.  Sending one would put an answer
             * into the reply stream of the client that no request asked for. */
            return;
        }
    }
    for (int index = 0; index < X_MAX_SELECTIONS; ++index) {
        if (server->selections[index].selection == 0U) {
            server->selections[index].selection = selection;
            server->selections[index].owner = owner;
            return;
        }
    }
    x_reply_error(client, BadAlloc, 0, selection);
}

static void request_get_selection_owner(struct x_server *server,
                                         struct x_client *client,
                                         struct x_reader *reader) {
    uint32_t selection = rd_u32(client, reader);
    uint32_t owner = 0U;
    for (int index = 0; index < X_MAX_SELECTIONS; ++index) {
        if (server->selections[index].selection == selection) {
            owner = server->selections[index].owner;
            break;
        }
    }
    xGetSelectionOwnerReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    reply.owner = (Window)client_u32(client, owner);
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_convert_selection(struct x_server *server,
                                      struct x_client *client,
                                      struct x_reader *reader) {
    uint32_t requestor = rd_u32(client, reader);
    uint32_t selection = rd_u32(client, reader);
    uint32_t target = rd_u32(client, reader);
    uint32_t property = rd_u32(client, reader);
    uint32_t time = rd_u32(client, reader);
    x_core_convert_selection(server, client, requestor, selection, target,
                             property, time);
}

/* ----------------------------------------------------------------- pixmaps */
static void request_create_pixmap(struct x_server *server, struct x_client *client,
                                  struct x_reader *reader) {
    /* The depth of the pixmap travels in the data byte of the request
     * header, and the body starts with the pixmap identifier. */
    uint16_t depth = client->request_minor;
    uint32_t id = rd_u32(client, reader);
    (void)rd_u32(client, reader);   /* the drawable the pixmap is made for */
    uint16_t width = rd_u16(client, reader);
    uint16_t height = rd_u16(client, reader);

    if (id == 0U || x_window_index(server, id) >= 0) {
        x_reply_error(client, BadIDChoice, 0, id);
        return;
    }
    for (int index = 0; index < X_MAX_PIXMAPS; ++index) {
        if (server->pixmaps[index].id == id) {
            x_reply_error(client, BadIDChoice, 0, id);
            return;
        }
    }
    if (depth != 1U && depth != 4U && depth != 8U && depth != 16U && depth != 24U &&
        depth != 32U) {
        x_reply_error(client, BadValue, 0, depth);
        return;
    }
    size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
    if (bytes == 0U || server->backing_used + bytes > X_BACKING_POOL_BYTES) {
        x_reply_error(client, BadAlloc, 0, id);
        return;
    }
    for (int index = 0; index < X_MAX_PIXMAPS; ++index) {
        if (server->pixmaps[index].id != 0U) continue;
        server->pixmaps[index].id = id;
        server->pixmaps[index].client = client_index_of(server, client);
        server->pixmaps[index].width = width;
        server->pixmaps[index].height = height;
        server->pixmaps[index].depth = depth;
        server->pixmaps[index].bytes = bytes;
        server->pixmaps[index].pixels = (uint32_t *)calloc(1, bytes);
        if (!server->pixmaps[index].pixels) {
            server->pixmaps[index].id = 0U;
            x_reply_error(client, BadAlloc, 0, id);
            return;
        }
        server->backing_used += bytes;
        /* The request has no reply. */
        return;
    }
    x_reply_error(client, BadAlloc, 0, id);
}

static void request_free_pixmap(struct x_server *server, struct x_client *client,
                                struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    for (int index = 0; index < X_MAX_PIXMAPS; ++index) {
        if (server->pixmaps[index].id != id) continue;
        if (server->backing_used >= server->pixmaps[index].bytes) {
            server->backing_used -= server->pixmaps[index].bytes;
        }
        free(server->pixmaps[index].pixels);
        memset(&server->pixmaps[index], 0, sizeof(server->pixmaps[index]));
        /* The request has no reply. */
        return;
    }
    x_reply_error(client, BadPixmap, 0, id);
}

/* ------------------------------------------------------------ graphics state */
static void request_create_gc(struct x_server *server, struct x_client *client,
                               struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint32_t drawable = rd_u32(client, reader);
    uint32_t value_mask = rd_u32(client, reader);

    if (!drawable_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    if (id == 0U || find_gc(server, id)) {
        x_reply_error(client, BadIDChoice, 0, id);
        return;
    }
    for (int index = 0; index < X_MAX_GCS; ++index) {
        if (server->gcs[index].id != 0U) continue;
        struct x_gc *gc = &server->gcs[index];
        memset(gc, 0, sizeof(*gc));
        gc->id = id;
        gc->client = client_index_of(server, client);
        gc->foreground = 0U;
        gc->background = 0x00ffffffU;
        gc->function = 3U;       /* GXcopy */
        gc->plane_mask = 0xffffffffU;
        gc->line_width = 0U;
        gc->fill_style = 0U;
        gc->subwindow_mode = 0U;
        gc->clip_width = 0xffffU;
        gc->clip_height = 0xffffU;
        gc->graphics_exposures = 1U;
        gc->font = -1;

        read_gc_values(client, reader, value_mask, gc);
        /* The request has no reply. */
        return;
    }
    x_reply_error(client, BadAlloc, 0, id);
}

static void request_change_gc(struct x_server *server, struct x_client *client,
                               struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint32_t value_mask = rd_u32(client, reader);
    struct x_gc *gc = find_gc(server, id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, id);
        return;
    }
    read_gc_values(client, reader, value_mask, gc);
    /* The request has no reply. */
}

static void request_copy_gc(struct x_server *server, struct x_client *client,
                            struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint32_t source_id = rd_u32(client, reader);
    uint32_t value_mask = rd_u32(client, reader);
    struct x_gc *source = find_gc(server, source_id);
    struct x_gc *gc = find_gc(server, id);
    if (!source) {
        x_reply_error(client, BadGC, 0, source_id);
        return;
    }
    if (!gc) {
        x_reply_error(client, BadGC, 0, id);
        return;
    }
    read_gc_values(client, reader, value_mask, gc);
    if (!(value_mask & (1U << 0))) gc->function = source->function;
    if (!(value_mask & (1U << 1))) gc->plane_mask = source->plane_mask;
    if (!(value_mask & (1U << 2))) gc->foreground = source->foreground;
    if (!(value_mask & (1U << 3))) gc->background = source->background;
    if (!(value_mask & (1U << 4))) gc->line_width = source->line_width;
    if (!(value_mask & (1U << 14))) gc->font = source->font;
    /* The request has no reply. */
}

static void request_free_gc(struct x_server *server, struct x_client *client,
                            struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    struct x_gc *gc = find_gc(server, id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, id);
        return;
    }
    memset(gc, 0, sizeof(*gc));
    /* The request has no reply. */
}

static void request_set_dashes(struct x_server *server, struct x_client *client,
                               struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint16_t offset = rd_u16(client, reader);
    uint8_t length = rd_u8(reader);
    (void)x_reader_skip(reader, 3U);
    struct x_gc *gc = find_gc(server, id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, id);
        return;
    }
    uint32_t dashes = 0U;
    for (uint32_t index = 0U; index < length; ++index) {
        dashes |= (uint32_t)rd_u8(reader) << (index * 8U);
    }
    gc->dash_offset = offset;
    gc->dashes = dashes;
    /* The request has no reply. */
}

static void request_set_clip_rectangles(struct x_server *server,
                                        struct x_client *client,
                                        struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint8_t ordering = rd_u8(reader);
    (void)x_reader_skip(reader, 3U);
    (void)ordering;
    struct x_gc *gc = find_gc(server, id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, id);
        return;
    }
    /* The rectangles are eight bytes each and the request ends with them. */
    size_t rectangles = reader->length - reader->position;
    rectangles /= 8U;
    if (rectangles > 0U) {
        int16_t x = (int16_t)rd_i16(client, reader);
        int16_t y = (int16_t)rd_i16(client, reader);
        uint16_t width = rd_u16(client, reader);
        uint16_t height = rd_u16(client, reader);
        gc->clip_x = x;
        gc->clip_y = y;
        gc->clip_width = width;
        gc->clip_height = height;
        reader->position += (rectangles - 1U) * 8U;
    }
    /* The request has no reply. */
}

/* ----------------------------------------------------------------- drawing */
static void request_clear_area(struct x_server *server, struct x_client *client,
                               struct x_reader *reader) {
    (void)client->request_minor;   /* the state of the area */
    uint32_t window = rd_u32(client, reader);
    int16_t x = rd_i16(client, reader);
    int16_t y = rd_i16(client, reader);
    uint16_t width = rd_u16(client, reader);
    uint16_t height = rd_u16(client, reader);
    int index = resolve_window(server, window);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, window);
        return;
    }
    x_drawable_fill(server, window, x, y, width, height,
                    server->windows[index].background_pixel);
    x_server_damage(server, index, x, y, (int16_t)width, (int16_t)height);
    x_server_repaint(server);
}

static void request_copy_area(struct x_server *server, struct x_client *client,
                              struct x_reader *reader) {
    uint32_t source = rd_u32(client, reader);
    uint32_t destination = rd_u32(client, reader);
    uint32_t gc_id = rd_u32(client, reader);
    int16_t source_x = rd_i16(client, reader);
    int16_t source_y = rd_i16(client, reader);
    int16_t destination_x = rd_i16(client, reader);
    int16_t destination_y = rd_i16(client, reader);
    uint16_t width = rd_u16(client, reader);
    uint16_t height = rd_u16(client, reader);
    struct x_gc *gc = find_gc(server, gc_id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, gc_id);
        return;
    }
    if (!drawable_exists(server, source)) {
        x_reply_error(client, BadDrawable, 0, source);
        return;
    }
    int destination_index = resolve_window(server, destination);
    if (destination_index < 0 && !x_pixmap_exists(server, destination)) {
        x_reply_error(client, BadDrawable, 0, destination);
        return;
    }
    x_drawable_copy(server, source, source_x, source_y, destination, destination_x,
                    destination_y, width, height);
    x_server_damage(server, destination_index, destination_x, destination_y,
                    (int16_t)width, (int16_t)height);
    x_server_repaint(server);
}

static void request_poly_point(struct x_server *server, struct x_client *client,
                                struct x_reader *reader) {
    uint32_t drawable = rd_u32(client, reader);
    uint32_t gc_id = rd_u32(client, reader);
    struct x_gc *gc = find_gc(server, gc_id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, gc_id);
        return;
    }
    if (!drawable_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    int index = resolve_window(server, drawable);
    while (x_reader_has(reader, 4U)) {
        int16_t x = rd_i16(client, reader);
        int16_t y = rd_i16(client, reader);
        x_drawable_put_pixel(server, drawable, x, y, gc->foreground);
        x_server_damage(server, index, x, y, 1, 1);
    }
    x_server_repaint(server);
}

static void request_poly_line(struct x_server *server, struct x_client *client,
                               struct x_reader *reader) {
    uint32_t drawable = rd_u32(client, reader);
    uint32_t gc_id = rd_u32(client, reader);
    struct x_gc *gc = find_gc(server, gc_id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, gc_id);
        return;
    }
    if (!drawable_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    int index = resolve_window(server, drawable);
    int have_first = 0;
    int16_t last_x = 0;
    int16_t last_y = 0;
    while (x_reader_has(reader, 4U)) {
        int16_t x = rd_i16(client, reader);
        int16_t y = rd_i16(client, reader);
        if (have_first) {
            x_drawable_line(server, drawable, last_x, last_y, x, y, gc->foreground);
            x_server_damage(server, index, last_x, last_y, (int16_t)(x - last_x),
                            (int16_t)(y - last_y));
        }
        have_first = 1;
        last_x = x;
        last_y = y;
    }
    x_server_repaint(server);
}

static void request_poly_segment(struct x_server *server, struct x_client *client,
                                 struct x_reader *reader) {
    uint32_t drawable = rd_u32(client, reader);
    uint32_t gc_id = rd_u32(client, reader);
    struct x_gc *gc = find_gc(server, gc_id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, gc_id);
        return;
    }
    if (!drawable_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    int index = resolve_window(server, drawable);
    while (x_reader_has(reader, 8U)) {
        int16_t x1 = rd_i16(client, reader);
        int16_t y1 = rd_i16(client, reader);
        int16_t x2 = rd_i16(client, reader);
        int16_t y2 = rd_i16(client, reader);
        x_drawable_line(server, drawable, x1, y1, x2, y2, gc->foreground);
        x_server_damage(server, index, x1, y1, (int16_t)(x2 - x1), (int16_t)(y2 - y1));
    }
    x_server_repaint(server);
}

static void request_poly_rectangle(struct x_server *server,
                                   struct x_client *client,
                                   struct x_reader *reader) {
    uint32_t drawable = rd_u32(client, reader);
    uint32_t gc_id = rd_u32(client, reader);
    struct x_gc *gc = find_gc(server, gc_id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, gc_id);
        return;
    }
    if (!drawable_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    int index = resolve_window(server, drawable);
    while (x_reader_has(reader, 8U)) {
        int16_t x = rd_i16(client, reader);
        int16_t y = rd_i16(client, reader);
        uint16_t width = rd_u16(client, reader);
        uint16_t height = rd_u16(client, reader);
        x_drawable_rectangle(server, drawable, x, y, width, height, gc->foreground);
        x_server_damage(server, index, x, y, (int16_t)width, (int16_t)height);
    }
    x_server_repaint(server);
}

static void request_poly_fill_rectangle(struct x_server *server,
                                        struct x_client *client,
                                        struct x_reader *reader) {
    uint32_t drawable = rd_u32(client, reader);
    uint32_t gc_id = rd_u32(client, reader);
    struct x_gc *gc = find_gc(server, gc_id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, gc_id);
        return;
    }
    if (!drawable_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    int index = resolve_window(server, drawable);
    while (x_reader_has(reader, 8U)) {
        int16_t x = rd_i16(client, reader);
        int16_t y = rd_i16(client, reader);
        uint16_t width = rd_u16(client, reader);
        uint16_t height = rd_u16(client, reader);
        x_drawable_fill(server, drawable, x, y, width, height, gc->foreground);
        x_server_damage(server, index, x, y, (int16_t)width, (int16_t)height);
    }
    x_server_repaint(server);
}

static void request_fill_poly(struct x_server *server, struct x_client *client,
                               struct x_reader *reader) {
    uint32_t drawable = rd_u32(client, reader);
    uint32_t gc_id = rd_u32(client, reader);
    /* The shape and the coordinate mode follow the graphics context, and the
     * points of the polygon start after them. */
    (void)x_reader_skip(reader, 4U);
    struct x_gc *gc = find_gc(server, gc_id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, gc_id);
        return;
    }
    if (!drawable_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    int index = resolve_window(server, drawable);
    /* The points of the polygon are drawn as a fan from the first point. */
    int have_first = 0;
    int16_t first_x = 0;
    int16_t first_y = 0;
    int16_t last_x = 0;
    int16_t last_y = 0;
    while (x_reader_has(reader, 4U)) {
        int16_t x = rd_i16(client, reader);
        int16_t y = rd_i16(client, reader);
        if (!have_first) {
            first_x = x;
            first_y = y;
            have_first = 1;
        } else {
            x_drawable_line(server, drawable, first_x, first_y, x, y, gc->foreground);
            x_drawable_line(server, drawable, last_x, last_y, x, y, gc->foreground);
        }
        last_x = x;
        last_y = y;
    }
    if (have_first) {
        x_drawable_line(server, drawable, first_x, first_y, last_x, last_y,
                        gc->foreground);
    }
    x_server_damage(server, index, 0, 0, server->width, server->height);
    x_server_repaint(server);
}

static void request_put_image(struct x_server *server, struct x_client *client,
                              struct x_reader *reader) {
    uint32_t drawable = rd_u32(client, reader);
    uint32_t gc_id = rd_u32(client, reader);
    uint16_t width = rd_u16(client, reader);
    uint16_t height = rd_u16(client, reader);
    int16_t destination_x = rd_i16(client, reader);
    int16_t destination_y = rd_i16(client, reader);
    uint8_t left_pad = rd_u8(reader);
    uint8_t depth = rd_u8(reader);
    (void)x_reader_skip(reader, 2U);
    struct x_gc *gc = find_gc(server, gc_id);
    if (!gc) {
        x_reply_error(client, BadGC, 0, gc_id);
        return;
    }
    if (!drawable_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    uint8_t format = client->request_minor;
    size_t bytes = 0U;
    if (format == 0U || depth == 1U) {
        size_t stride = ((size_t)width + (size_t)left_pad + 31U) / 32U * 4U;
        bytes = stride * (size_t)height;
    } else if (format == 1U) {
        size_t stride = ((size_t)width + (size_t)left_pad + 31U) / 32U * 4U;
        bytes = stride * (size_t)height * (size_t)depth;
    } else {
        if (depth <= 8U) {
            bytes = (((size_t)width + 3U) / 4U * 4U) * (size_t)height;
        } else if (depth <= 16U) {
            bytes = (((size_t)width * 2U + 3U) / 4U * 4U) * (size_t)height;
        } else {
            bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
        }
    }
    const uint8_t *pixels = x_reader_bytes(reader, bytes);
    if (!pixels) {
        x_reply_error(client, BadLength, 0, 0U);
        return;
    }
    (void)left_pad;
    x_drawable_image(server, drawable, destination_x, destination_y, width, height,
                     depth, (uint16_t)format, pixels);
    int index = resolve_window(server, drawable);
    x_server_damage(server, index, destination_x, destination_y, (int16_t)width,
                    (int16_t)height);
    x_server_repaint(server);
}

static void request_get_image(struct x_server *server, struct x_client *client,
                              struct x_reader *reader) {
    uint32_t drawable = rd_u32(client, reader);
    int16_t x = rd_i16(client, reader);
    int16_t y = rd_i16(client, reader);
    uint16_t width = rd_u16(client, reader);
    uint16_t height = rd_u16(client, reader);
    uint32_t plane_mask = rd_u32(client, reader);
    /* The format of the image travels in the data byte of the request
     * header. */
    uint8_t format = client->request_minor;
    if (!drawable_exists(server, drawable)) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    uint16_t drawable_width = 0;
    uint16_t drawable_height = 0;
    int is_window = 0;
    uint32_t *pixels = NULL;
    if (drawable == X_ROOT_WINDOW_ID && server->screen) {
        /* Reading the root window returns the composited screen image so
         * callers see mapped frames and child windows as drawn on screen. */
        drawable_width = (uint16_t)server->width;
        drawable_height = (uint16_t)server->height;
        is_window = 1;
        pixels = server->screen;
    } else {
        pixels = x_drawable_pixels(server, drawable, &drawable_width,
                                   &drawable_height, &is_window);
    }
    if (!pixels) {
        x_reply_error(client, BadDrawable, 0, drawable);
        return;
    }
    size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
    uint8_t buffer[32];
    reply_begin(client, buffer, 24U, (uint32_t)(bytes / 4U));
    reply_put_u32(client, buffer, 8, (uint32_t)client_u32(client, plane_mask));
    x_reply_bytes(client, buffer, sizeof(buffer));
    uint8_t row[512];
    for (uint16_t line = 0U; line < height; ++line) {
        size_t offset = 0U;
        memset(row, 0, sizeof(row));
        for (uint16_t column = 0U; column < width; ++column) {
            int source_x = x + column;
            int source_y = y + line;
            uint32_t pixel = 0U;
            if (source_x >= 0 && source_x < (int)drawable_width && source_y >= 0 &&
                source_y < (int)drawable_height) {
                pixel = pixels[(size_t)source_y * (size_t)drawable_width +
                               (size_t)source_x];
            }
            memcpy(row + offset, &pixel, 4);
            offset += 4U;
            if (offset + 4U > sizeof(row)) {
                x_reply_bytes(client, row, offset);
                offset = 0U;
            }
        }
        if (offset > 0U) x_reply_bytes(client, row, offset);
    }
    (void)format;
}

/* -------------------------------------------------------------------- text */
static void request_poly_text(struct x_server *server, struct x_client *client,
                              struct x_reader *reader, int sixteen_bit) {
    uint32_t drawable = rd_u32(client, reader);
    uint32_t gc_id = rd_u32(client, reader);
    int16_t x = rd_i16(client, reader);
    int16_t y = rd_i16(client, reader);
    struct x_text_item items[X_MAX_TEXT_ITEMS];
    uint8_t storage[X_MAX_TEXT_BYTES];
    size_t item_count = parse_text_items(reader, sixteen_bit, items,
                                         X_MAX_TEXT_ITEMS, storage,
                                         sizeof(storage));
    if (item_count == 0U) {
        x_reply_error(client, BadLength, 0, 0U);
        return;
    }
    x_core_poly_text(server, client, drawable, gc_id, x, y, items, item_count);
}

static void request_image_text(struct x_server *server, struct x_client *client,
                               struct x_reader *reader, int sixteen_bit) {
    uint32_t drawable = rd_u32(client, reader);
    uint32_t gc_id = rd_u32(client, reader);
    int16_t x = rd_i16(client, reader);
    int16_t y = rd_i16(client, reader);
    /* The number of characters travels in the data byte of the request
     * header, not in the body of the request. */
    uint16_t count = client->request_minor;
    uint8_t text[X_MAX_TEXT_BYTES];
    if (sixteen_bit) {
        const uint8_t *wide = x_reader_bytes(reader, (size_t)count * 2U);
        if (!wide) {
            x_reply_error(client, BadLength, 0, count);
            return;
        }
        for (uint32_t index = 0U; index < count; ++index) {
            text[index] = wide[index * 2U];
        }
    } else {
        const uint8_t *narrow = x_reader_bytes(reader, count);
        if (!narrow) {
            x_reply_error(client, BadLength, 0, count);
            return;
        }
        memcpy(text, narrow, count);
    }
    x_core_image_text(server, client, drawable, gc_id, x, y, text, count);
}

/* ------------------------------------------------------------------- input */
static void request_send_event(struct x_server *server, struct x_client *client,
                               struct x_reader *reader) {
    uint32_t destination = rd_u32(client, reader);
    uint32_t event_mask = rd_u32(client, reader);
    /* Whether the event is propagated travels in the data byte of the request
     * header, and the event itself follows the mask in the body. */
    int propagate = client->request_minor ? 1 : 0;
    uint8_t event[32];
    memset(event, 0, sizeof(event));
    for (uint32_t index = 0U; index < 32U; ++index) {
        event[index] = rd_u8(reader);
    }
    int index = resolve_window(server, destination);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, destination);
        return;
    }
    uint32_t mask = propagate ? event_mask : event_mask;
    if (mask == 0U) {
        int owner_client = server->windows[index].client;
        if (owner_client >= 0 && server->clients[owner_client].in_use) {
            x_event_queue(&server->clients[owner_client], event, sizeof(event),
                          0U);
        }
        return;
    }
    for (int slot = 0; slot < X_MAX_CLIENTS; ++slot) {
        if (!server->clients[slot].in_use) continue;
        if ((x_window_event_mask(server, index, slot) & mask) == 0U) continue;
        x_event_queue(&server->clients[slot], event, sizeof(event), 0U);
    }
    /* The request has no reply. */
}

static void request_grab_pointer(struct x_server *server, struct x_client *client,
                                 struct x_reader *reader) {
    (void)rd_u32(client, reader);   /* the window that is grabbed */
    (void)rd_u16(client, reader);   /* the events that are reported */
    (void)rd_u8(reader);            /* the pointer mode */
    (void)rd_u8(reader);            /* the keyboard mode */
    (void)rd_u32(client, reader);   /* the window the pointer is confined to */
    (void)rd_u32(client, reader);   /* the cursor */
    (void)rd_u32(client, reader);   /* the time */
    x_core_grab_pointer(server, client);
}

static void request_grab_keyboard(struct x_server *server,
                                  struct x_client *client,
                                  struct x_reader *reader) {
    (void)rd_u32(client, reader);   /* the window that is grabbed */
    (void)rd_u32(client, reader);   /* the time */
    (void)rd_u8(reader);            /* the pointer mode */
    (void)rd_u8(reader);            /* the keyboard mode */
    x_core_grab_keyboard(server, client);
}

static void request_grab_button(struct x_server *server, struct x_client *client,
                                struct x_reader *reader) {
    (void)server;
    (void)rd_u32(client, reader);   /* the window that is grabbed */
    (void)rd_u16(client, reader);   /* the events that are reported */
    (void)rd_u8(reader);            /* the pointer mode */
    (void)rd_u8(reader);            /* the keyboard mode */
    (void)rd_u32(client, reader);   /* the window the pointer is confined to */
    (void)rd_u32(client, reader);   /* the cursor */
    (void)rd_u8(reader);            /* the button */
    (void)rd_u8(reader);            /* the padding */
    (void)rd_u16(client, reader);   /* the modifiers */
    xGrabPointerReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.status = (CARD8)client_u8(client, GrabSuccess);
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_query_pointer(struct x_server *server, struct x_client *client,
                                  struct x_reader *reader) {
    uint32_t window = rd_u32(client, reader);
    int index = resolve_window(server, window);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, window);
        return;
    }
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    x_window_absolute_origin(server, index, &origin_x, &origin_y);
    int child = x_window_pointer_child(server, server->pointer_x, server->pointer_y);
    xQueryPointerReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sameScreen = (BOOL)client_u8(client, 1U);
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    reply.root = (Window)client_u32(client, server->windows[0].id);
    reply.child = (Window)client_u32(client,
                              child >= 0 ? server->windows[child].id : 0U);
    reply.rootX = (INT16)client_u16(client, (uint16_t)server->pointer_x);
    reply.rootY = (INT16)client_u16(client, (uint16_t)server->pointer_y);
    reply.winX = (INT16)client_u16(client, (uint16_t)(server->pointer_x - origin_x));
    reply.winY = (INT16)client_u16(client, (uint16_t)(server->pointer_y - origin_y));
    reply.mask = (CARD16)client_u16(client, (uint16_t)server->button_mask);
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_translate_coordinates(struct x_server *server,
                                           struct x_client *client,
                                           struct x_reader *reader) {
    uint32_t source = rd_u32(client, reader);
    uint32_t destination = rd_u32(client, reader);
    int16_t source_x = rd_i16(client, reader);
    int16_t source_y = rd_i16(client, reader);
    x_core_translate_coordinates(server, client, source, destination, source_x,
                                 source_y);
}

static void request_warp_pointer(struct x_server *server, struct x_client *client,
                                 struct x_reader *reader) {
    uint32_t source = rd_u32(client, reader);
    uint32_t destination = rd_u32(client, reader);
    int16_t source_x = rd_i16(client, reader);
    int16_t source_y = rd_i16(client, reader);
    int16_t destination_x = rd_i16(client, reader);
    int16_t destination_y = rd_i16(client, reader);
    x_core_warp_pointer(server, client, source, source_x, source_y, destination,
                        destination_x, destination_y);
}

static void request_set_input_focus(struct x_server *server,
                                    struct x_client *client,
                                    struct x_reader *reader) {
    uint32_t focus = rd_u32(client, reader);
    /* The way the focus returns travels in the data byte of the request
     * header. */
    (void)client->request_minor;
    uint32_t time = rd_u32(client, reader);
    (void)time;
    int index = resolve_window(server, focus);
    if (index < 0) {
        x_reply_error(client, BadWindow, 0, focus);
        return;
    }
    server->focus_window = index;
    x_event_focus(server, index, 1U);
    /* The request has no reply. */
}

static void request_get_input_focus(struct x_server *server,
                                    struct x_client *client,
                                    struct x_reader *reader) {
    (void)reader;
    int index = server->focus_window;
    xGetInputFocusReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.revertTo = (CARD8)client_u8(client, 2U);
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    reply.focus = (Window)client_u32(client,
                            index >= 0 ? server->windows[index].id : 0U);
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_query_keymap(struct x_server *server, struct x_client *client,
                                 struct x_reader *reader) {
    (void)reader;
    xQueryKeymapReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)((sizeof(reply) - 32U) / 4U);
    for (uint32_t index = 0U; index < 32U; ++index) {
        reply.map[index] = server->key_down[index];
    }
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_get_keyboard_mapping(struct x_server *server,
                                         struct x_client *client,
                                         struct x_reader *reader) {
    uint8_t first_keycode = rd_u8(reader);
    uint8_t count = rd_u8(reader);
    (void)x_reader_skip(reader, 2U);
    uint32_t produced = 0U;
    uint8_t keycodes[256];
    for (uint32_t index = 0U; index < count; ++index) {
        uint32_t code = (uint32_t)first_keycode + index;
        if (code > 255U) break;
        keycodes[produced] = (uint8_t)code;
        ++produced;
    }
    /* A real keyboard reports two keysyms for every keycode, the unshifted one
     * and the shifted one, and a client reads exactly that many keysyms for
     * every keycode it asked for.  Answering with a different number of
     * keysyms than the reply announces leaves the client reading the rest of
     * the answer at the wrong place. */
    uint32_t per_keycode = 2U;
    xGetKeyboardMappingReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.keySymsPerKeyCode = (CARD8)client_u8(client, (uint8_t)per_keycode);
    reply.sequenceNumber = (CARD16)client->sequence;
    /* The length counts four byte units of additional data, and one keysym is
     * exactly one such unit. */
    reply.length = (CARD32)(produced * per_keycode);
    x_reply_bytes(client, &reply, sizeof(reply));
    for (uint32_t index = 0U; index < produced; ++index) {
        uint32_t code = keycodes[index];
        uint32_t lower = x_keysym_for_keycode(server, code);
        uint32_t upper = lower | 0xff00U;
        uint8_t entry[8];
        reply_put_u32(client, entry, 0, (uint32_t)client_u32(client, lower));
        reply_put_u32(client, entry, 4, (uint32_t)client_u32(client, upper));
        x_reply_bytes(client, entry, sizeof(entry));
    }
}

/* The modifier map of the X11 protocol holds the key codes of the keys that
 * carry each of the eight modifiers.  Every group holds the same number of
 * key codes, and the reply announces that number, because a client reads
 * exactly that many bytes for each group.  A reply that announces a number
 * other than the one it carries makes the client reject the whole map, and a
 * client that is handed no map dereferences the empty result. */
#define X_MODIFIER_GROUPS 8U
#define X_MODIFIER_KEYS_PER_GROUP 2U

static const uint8_t modifier_keycodes[X_MODIFIER_GROUPS *
                                       X_MODIFIER_KEYS_PER_GROUP] = {
    50, 62,   /* Shift */
    66, 0,    /* Lock (Caps Lock) */
    37, 105,  /* Control */
    64, 108,  /* Mod1 (Alt) */
    77, 0,    /* Mod2 (Num Lock) */
    78, 0,    /* Mod3 (Scroll Lock) */
    0, 0,     /* Mod4 (Super) */
    0, 0      /* Mod5 */
};

static void request_get_modifier_mapping(struct x_server *server,
                                         struct x_client *client,
                                         struct x_reader *reader) {
    (void)server;
    (void)reader;
    xGetModifierMappingReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.numKeyPerModifier = (CARD8)X_MODIFIER_KEYS_PER_GROUP;
    reply.sequenceNumber = (CARD16)client->sequence;
    /* The data holds eight groups of the announced number of key codes, and
     * the length counts four byte units of it. */
    reply.length = (CARD32)((X_MODIFIER_GROUPS * X_MODIFIER_KEYS_PER_GROUP) /
                             4U);
    x_reply_bytes(client, &reply, sizeof(reply));
    for (uint32_t index = 0U;
         index < X_MODIFIER_GROUPS * X_MODIFIER_KEYS_PER_GROUP; ++index) {
        x_reply_bytes(client, &modifier_keycodes[index], 1U);
    }
}

static void request_get_keyboard_control(struct x_server *server,
                                         struct x_client *client,
                                         struct x_reader *reader) {
    (void)server;
    (void)reader;
    xGetKeyboardControlReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)((sizeof(reply) - 32U) / 4U);
    reply.globalAutoRepeat = (BOOL)client_u8(client, 1U);
    reply.ledMask = (CARD32)client_u32(client, 0U);
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_get_pointer_control(struct x_server *server,
                                        struct x_client *client,
                                        struct x_reader *reader) {
    (void)server;
    (void)reader;
    xGetPointerControlReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)((sizeof(reply) - 32U) / 4U);
    reply.accelNumerator = (CARD16)client_u16(client, 2U);
    reply.accelDenominator = (CARD16)client_u16(client, 1U);
    reply.threshold = (CARD16)client_u16(client, 1U);
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_get_screen_saver(struct x_server *server,
                                     struct x_client *client,
                                     struct x_reader *reader) {
    (void)server;
    (void)reader;
    xGetScreenSaverReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)((sizeof(reply) - 32U) / 4U);
    reply.timeout = (CARD16)client_u16(client, 0U);
    reply.interval = (CARD16)client_u16(client, 0U);
    reply.preferBlanking = (BOOL)client_u8(client, 0U);
    reply.allowExposures = (BOOL)client_u8(client, 1U);
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_get_pointer_mapping(struct x_server *server,
                                        struct x_client *client,
                                        struct x_reader *reader) {
    (void)server;
    (void)reader;
    xGetPointerMappingReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;
    reply.nElts = (CARD8)client_u8(client, 0U);
    x_reply_bytes(client, &reply, sizeof(reply));
}

/* ------------------------------------------------------------------ fonts */
static void request_open_font(struct x_server *server, struct x_client *client,
                              struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint16_t name_length = rd_u16(client, reader);
    (void)x_reader_skip(reader, 2U);
    const uint8_t *name = x_reader_bytes(reader, name_length);
    if (!name) {
        x_reply_error(client, BadLength, 0, 0U);
        return;
    }
    x_core_open_font(server, client, id, (const char *)name, name_length);
}

static void request_close_font(struct x_server *server, struct x_client *client,
                               struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    (void)client;
    x_core_close_font(server, id);
    /* The request has no reply. */
}

/* ----------------------------------------------------------------- colours */
static void request_create_colormap(struct x_server *server,
                                    struct x_client *client,
                                    struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint32_t window = rd_u32(client, reader);
    uint32_t visual = rd_u32(client, reader);
    x_core_create_colormap(server, client, id, window, visual);
    /* The request has no reply. */
}

static void request_alloc_color(struct x_server *server, struct x_client *client,
                                struct x_reader *reader) {
    uint32_t colormap = rd_u32(client, reader);
    uint16_t red = rd_u16(client, reader);
    uint16_t green = rd_u16(client, reader);
    uint16_t blue = rd_u16(client, reader);
    (void)x_reader_skip(reader, 2U);
    x_core_alloc_color(server, client, colormap, red, green, blue);
}

static void request_alloc_named_color(struct x_server *server,
                                      struct x_client *client,
                                      struct x_reader *reader) {
    uint32_t colormap = rd_u32(client, reader);
    uint16_t name_length = rd_u16(client, reader);
    (void)x_reader_skip(reader, 2U);
    const uint8_t *name = x_reader_bytes(reader, name_length);
    if (!name) {
        x_reply_error(client, BadLength, 0, 0U);
        return;
    }
    x_core_alloc_named_color(server, client, colormap, (const char *)name,
                              name_length);
}

static void request_lookup_color(struct x_server *server, struct x_client *client,
                                 struct x_reader *reader) {
    uint32_t colormap = rd_u32(client, reader);
    uint16_t name_length = rd_u16(client, reader);
    (void)x_reader_skip(reader, 2U);
    const uint8_t *name = x_reader_bytes(reader, name_length);
    if (!name) {
        x_reply_error(client, BadLength, 0, 0U);
        return;
    }
    x_core_lookup_color(server, client, colormap, (const char *)name, name_length);
}

static void request_query_colors(struct x_server *server, struct x_client *client,
                                 struct x_reader *reader) {
    uint32_t colormap = rd_u32(client, reader);
    uint32_t pixels[256];
    uint32_t count = 0U;
    while (x_reader_has(reader, 4U) && count < 256U) {
        pixels[count++] = rd_u32(client, reader);
    }
    x_core_query_colors(server, client, colormap, pixels, count);
}

/* ----------------------------------------------------------------- cursors */
static void request_create_cursor(struct x_server *server,
                                  struct x_client *client,
                                  struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint32_t source = rd_u32(client, reader);
    uint32_t mask = rd_u32(client, reader);
    uint16_t fore_red = rd_u16(client, reader);
    uint16_t fore_green = rd_u16(client, reader);
    uint16_t fore_blue = rd_u16(client, reader);
    uint16_t back_red = rd_u16(client, reader);
    uint16_t back_green = rd_u16(client, reader);
    uint16_t back_blue = rd_u16(client, reader);
    uint16_t hot_x = rd_u16(client, reader);
    uint16_t hot_y = rd_u16(client, reader);
    (void)fore_red;
    (void)fore_green;
    (void)fore_blue;
    (void)back_red;
    (void)back_green;
    (void)back_blue;
    if (!drawable_exists(server, source)) {
        x_reply_error(client, BadDrawable, 0, source);
        return;
    }
    for (int index = 0; index < X_MAX_CURSORS; ++index) {
        if (server->cursors[index].id != 0U) continue;
        server->cursors[index].id = id;
        server->cursors[index].client = client_index_of(server, client);
        server->cursors[index].source_x = 0U;
        server->cursors[index].source_y = 0U;
        server->cursors[index].hot_x = hot_x;
        server->cursors[index].hot_y = hot_y;
        (void)mask;
        /* The request has no reply. */
        return;
    }
    x_reply_error(client, BadAlloc, 0, id);
}

static void request_create_glyph_cursor(struct x_server *server,
                                       struct x_client *client,
                                       struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    uint32_t source = rd_u32(client, reader);
    uint32_t mask = rd_u32(client, reader);
    uint16_t source_char = rd_u16(client, reader);
    uint16_t mask_char = rd_u16(client, reader);
    (void)rd_u16(client, reader);
    (void)rd_u16(client, reader);
    (void)rd_u16(client, reader);
    (void)rd_u16(client, reader);
    (void)rd_u16(client, reader);
    (void)rd_u16(client, reader);
    (void)source_char;
    (void)mask_char;
    for (int index = 0; index < X_MAX_CURSORS; ++index) {
        if (server->cursors[index].id != 0U) continue;
        server->cursors[index].id = id;
        server->cursors[index].client = client_index_of(server, client);
        server->cursors[index].source_x = 0U;
        server->cursors[index].source_y = 0U;
        server->cursors[index].hot_x = 0U;
        server->cursors[index].hot_y = 0U;
        (void)source;
        (void)mask;
        /* The request has no reply. */
        return;
    }
    x_reply_error(client, BadAlloc, 0, id);
}

static void request_free_cursor(struct x_server *server, struct x_client *client,
                                 struct x_reader *reader) {
    uint32_t id = rd_u32(client, reader);
    for (int index = 0; index < X_MAX_CURSORS; ++index) {
        if (server->cursors[index].id != id) continue;
        memset(&server->cursors[index], 0, sizeof(server->cursors[index]));
        /* The request has no reply. */
        return;
    }
    x_reply_error(client, BadCursor, 0, id);
}

/* -------------------------------------------------------------- extensions */
#define X_EXT_SHAPE_OPCODE 128
#define X_EXT_SHAPE_EVENT  64
#define X_EXT_XRES_OPCODE  129

static void dispatch_shape_request(struct x_server *server,
                                   struct x_client *client,
                                   struct x_reader *reader) {
    (void)server;
    uint8_t minor = client->request_minor;
    if (minor == 0) { /* X_ShapeQueryVersion */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply[0] = X_Reply;
        reply_put_u16(client, reply, 2, (uint16_t)client->sequence);
        reply_put_u32(client, reply, 4, 0U); /* length = 0 */
        reply_put_u16(client, reply, 8, 1U); /* major = 1 */
        reply_put_u16(client, reply, 10, 1U); /* minor = 1 */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 5) { /* X_ShapeQueryExtents */
        uint32_t window_id = rd_u32(client, reader);
        int index = x_window_index(server, window_id);
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply[0] = X_Reply;
        reply_put_u16(client, reply, 2, (uint16_t)client->sequence);
        reply_put_u32(client, reply, 4, 0U);
        reply[8] = 0U; /* boundingShaped = False */
        reply[9] = 0U; /* clipShaped = False */
        if (index >= 0) {
            struct x_window *win = &server->windows[index];
            reply_put_u16(client, reply, 12, (uint16_t)win->x);
            reply_put_u16(client, reply, 14, (uint16_t)win->y);
            reply_put_u16(client, reply, 16, win->width);
            reply_put_u16(client, reply, 18, win->height);
            reply_put_u16(client, reply, 20, (uint16_t)win->x);
            reply_put_u16(client, reply, 22, (uint16_t)win->y);
            reply_put_u16(client, reply, 24, win->width);
            reply_put_u16(client, reply, 26, win->height);
        }
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 7) { /* X_ShapeInputSelected */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply[0] = X_Reply;
        reply_put_u16(client, reply, 2, (uint16_t)client->sequence);
        reply_put_u32(client, reply, 4, 0U);
        reply[8] = 0U; /* enabled = False */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 8) { /* X_ShapeGetRectangles */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply[0] = X_Reply;
        reply[1] = 0U; /* Unsorted */
        reply_put_u16(client, reply, 2, (uint16_t)client->sequence);
        reply_put_u32(client, reply, 4, 0U);
        reply_put_u32(client, reply, 8, 0U); /* nrects = 0 */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    /* Mutation requests like ShapeRectangles, Mask, Combine, Offset, SelectInput have no reply. */
}

static void dispatch_xres_request(struct x_server *server,
                                  struct x_client *client,
                                  struct x_reader *reader) {
    (void)server;
    (void)reader;
    uint8_t minor = client->request_minor;
    if (minor == 0) { /* X_XResQueryVersion */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply[0] = X_Reply;
        reply_put_u16(client, reply, 2, (uint16_t)client->sequence);
        reply_put_u32(client, reply, 4, 0U);
        reply_put_u16(client, reply, 8, 1U);  /* major = 1 */
        reply_put_u16(client, reply, 10, 2U); /* minor = 2 */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 1) { /* X_XResQueryClients */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply[0] = X_Reply;
        reply_put_u16(client, reply, 2, (uint16_t)client->sequence);
        reply_put_u32(client, reply, 4, 0U);
        reply_put_u32(client, reply, 8, 0U); /* num_clients = 0 */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    if (minor == 4) { /* X_XResQueryClientIds */
        uint8_t reply[32];
        memset(reply, 0, sizeof(reply));
        reply[0] = X_Reply;
        reply_put_u16(client, reply, 2, (uint16_t)client->sequence);
        reply_put_u32(client, reply, 4, 0U);
        reply_put_u32(client, reply, 8, 0U); /* num_ids = 0 */
        x_reply_bytes(client, reply, sizeof(reply));
        return;
    }
    /* Unknown or unsupported minor request */
    x_reply_error(client, BadRequest, client->request_major, 0U);
}

static void request_query_extension(struct x_server *server,
                                    struct x_client *client,
                                    struct x_reader *reader) {
    (void)server;
    uint16_t name_length = rd_u16(client, reader);
    (void)x_reader_skip(reader, 2U);
    const uint8_t *name = x_reader_bytes(reader, name_length);
    if (!name) {
        x_reply_error(client, BadLength, 0, 0U);
        return;
    }

    xQueryExtensionReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = 0U;

    if (name_length == 5 && memcmp(name, "SHAPE", 5) == 0) {
        reply.present = (BOOL)client_u8(client, 1U);
        reply.major_opcode = (CARD8)client_u8(client, X_EXT_SHAPE_OPCODE);
        reply.first_event = (CARD8)client_u8(client, X_EXT_SHAPE_EVENT);
        reply.first_error = (CARD8)client_u8(client, 0U);
    } else if (name_length == 10 && memcmp(name, "X-Resource", 10) == 0) {
        reply.present = (BOOL)client_u8(client, 1U);
        reply.major_opcode = (CARD8)client_u8(client, X_EXT_XRES_OPCODE);
        reply.first_event = (CARD8)client_u8(client, 0U);
        reply.first_error = (CARD8)client_u8(client, 0U);
    } else {
        reply.present = (BOOL)client_u8(client, 0U);
        reply.major_opcode = (CARD8)client_u8(client, 0U);
        reply.first_event = (CARD8)client_u8(client, 0U);
        reply.first_error = (CARD8)client_u8(client, 0U);
    }
    x_reply_bytes(client, &reply, sizeof(reply));
}

static void request_list_extensions(struct x_server *server,
                                    struct x_client *client,
                                    struct x_reader *reader) {
    (void)server;
    (void)reader;
    static const char *exts[] = {"SHAPE", "X-Resource"};
    size_t count = sizeof(exts) / sizeof(exts[0]);
    uint8_t buffer[64];
    size_t used = 0U;
    for (size_t i = 0; i < count; ++i) {
        size_t len = strlen(exts[i]);
        buffer[used++] = (uint8_t)len;
        memcpy(buffer + used, exts[i], len);
        used += len;
    }
    size_t padded = (used + 3U) & ~3U;
    while (used < padded) buffer[used++] = 0U;

    xListExtensionsReply reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = X_Reply;
    reply.nExtensions = (CARD8)client_u8(client, (uint8_t)count);
    reply.sequenceNumber = (CARD16)client->sequence;
    reply.length = (CARD32)client_u32(client, (uint32_t)(padded / 4U));
    x_reply_bytes(client, &reply, sizeof(reply));
    if (padded) {
        x_reply_bytes(client, buffer, padded);
    }
}

/* --------------------------------------------------------------- dispatcher */
static void x_dispatch(struct x_server *server, struct x_client *client,
                        const struct x_request *request) {
    struct x_reader reader;
    x_reader_init(&reader, request->body, request->body_length);
    ++client->sequence;
    ++server->sequence;
    client->request_major = request->type;
    client->request_minor = request->detail;

    if (server_trace_enabled) {
        printf("xserver: t%llu c%d request %u units %u seq %u\n",
               (unsigned long long)server_now_millis(), client->fd,
               (unsigned)request->type, (unsigned)request->length_units,
               (unsigned)client->sequence);
        fflush(stdout);
    }

    switch (request->type) {
    case X_CreateWindow:
        request_create_window(server, client, &reader);
        break;
    case X_ChangeWindowAttributes:
        request_change_window_attributes(server, client, &reader);
        break;
    case X_GetWindowAttributes:
        request_get_window_attributes(server, client, &reader);
        break;
    case X_DestroyWindow:
        request_destroy_window(server, client, &reader);
        break;
    case X_DestroySubwindows: {
        uint32_t id = rd_u32(client, &reader);
        int index = resolve_window(server, id);
        if (index < 0) {
            x_reply_error(client, BadWindow, 0, id);
            break;
        }
        for (int child = server->windows[index].first_child; child >= 0;) {
            int next = server->windows[child].next_sibling;
            x_window_destroy(server, child);
            child = next;
        }
        x_server_repaint(server);
        break;
    }
    case X_ChangeSaveSet:
        /* The request has no reply and the server keeps no save set. */
        break;
    case X_ReparentWindow:
        request_reparent_window(server, client, &reader);
        break;
    case X_MapWindow:
        request_map_window(server, client, &reader);
        break;
    case X_MapSubwindows: {
        uint32_t id = rd_u32(client, &reader);
        int index = resolve_window(server, id);
        if (index < 0) {
            x_reply_error(client, BadWindow, 0, id);
            break;
        }
        for (int child = server->windows[index].first_child; child >= 0;
             child = server->windows[child].next_sibling) {
            x_window_map(server, child);
        }
        x_server_repaint(server);
        break;
    }
    case X_UnmapWindow:
        request_unmap_window(server, client, &reader);
        break;
    case X_UnmapSubwindows: {
        uint32_t id = rd_u32(client, &reader);
        int index = resolve_window(server, id);
        if (index < 0) {
            x_reply_error(client, BadWindow, 0, id);
            break;
        }
        for (int child = server->windows[index].first_child; child >= 0;
             child = server->windows[child].next_sibling) {
            x_window_unmap(server, child);
        }
        x_server_repaint(server);
        break;
    }
    case X_ConfigureWindow:
        request_configure_window(server, client, &reader);
        break;
    case X_CirculateWindow: {
        uint32_t id = rd_u32(client, &reader);
        (void)rd_u8(&reader);
        (void)x_reader_skip(&reader, 3U);
        int index = resolve_window(server, id);
        if (index < 0) {
            x_reply_error(client, BadWindow, 0, id);
            break;
        }
        x_server_repaint(server);
        break;
    }
    case X_GetGeometry:
        request_get_geometry(server, client, &reader);
        break;
    case X_QueryTree:
        request_query_tree(server, client, &reader);
        break;
    case X_InternAtom:
        request_intern_atom(server, client, &reader);
        break;
    case X_GetAtomName:
        request_get_atom_name(server, client, &reader);
        break;
    case X_ChangeProperty:
        request_change_property(server, client, &reader);
        break;
    case X_DeleteProperty:
        request_delete_property(server, client, &reader);
        break;
    case X_GetProperty:
        request_get_property(server, client, &reader);
        break;
    case X_ListProperties:
        request_list_properties(server, client, &reader);
        break;
    case X_SetSelectionOwner:
        request_set_selection_owner(server, client, &reader);
        break;
    case X_GetSelectionOwner:
        request_get_selection_owner(server, client, &reader);
        break;
    case X_ConvertSelection:
        request_convert_selection(server, client, &reader);
        break;
    case X_SendEvent:
        request_send_event(server, client, &reader);
        break;
    case X_GrabPointer:
        request_grab_pointer(server, client, &reader);
        break;
    case X_UngrabPointer:
        /* The request has no reply. */
        break;
    case X_GrabButton:
        request_grab_button(server, client, &reader);
        break;
    case X_UngrabButton:
        /* The request has no reply. */
        break;
    case X_ChangeActivePointerGrab:
        /* The request has no reply. */
        break;
    case X_GrabKeyboard:
        request_grab_keyboard(server, client, &reader);
        break;
    case X_UngrabKeyboard:
        /* The request has no reply. */
        break;
    case X_GrabKey:
    case X_UngrabKey: {
        /* A key grab has no reply.  The grab itself is recorded, so that the
         * key event reaches the window manager that asked for it instead of
         * the window that holds the focus. */
        uint32_t owner = rd_u32(client, &reader);
        uint16_t modifiers = rd_u16(client, &reader);
        /* The key of a GrabKey request travels in the body of the request,
         * while the key of an UngrabKey request travels in the data byte of
         * the request header. */
        uint8_t key = (request->type == X_GrabKey) ? rd_u8(&reader) : request->detail;
        if (request->type == X_GrabKey) {
            x_server_grab_key(server, client_index_of(server, client), owner,
                              modifiers, key);
        } else {
            x_server_ungrab_key(server, client_index_of(server, client), owner,
                                modifiers, key);
        }
        break;
    }
    case X_AllowEvents:
    case X_GrabServer:
    case X_UngrabServer:
        /* These requests have no reply. */
        break;
    case X_QueryPointer:
        request_query_pointer(server, client, &reader);
        break;
    case X_GetMotionEvents: {
        /* The reply carries no event, because the server keeps no history. */
        uint8_t buffer[32];
        reply_begin(client, buffer, 0U, 0U);
        x_reply_bytes(client, buffer, sizeof(buffer));
        break;
    }
    case X_TranslateCoords:
        request_translate_coordinates(server, client, &reader);
        break;
    case X_WarpPointer:
        request_warp_pointer(server, client, &reader);
        break;
    case X_SetInputFocus:
        request_set_input_focus(server, client, &reader);
        break;
    case X_GetInputFocus:
        request_get_input_focus(server, client, &reader);
        break;
    case X_QueryKeymap:
        request_query_keymap(server, client, &reader);
        break;
    case X_OpenFont:
        request_open_font(server, client, &reader);
        break;
    case X_CloseFont:
        request_close_font(server, client, &reader);
        break;
    case X_GetFontPath:
        x_core_get_font_path(server, client);
        break;
    case X_ListFonts:
        x_core_list_fonts(server, client);
        break;
    case X_CreatePixmap:
        request_create_pixmap(server, client, &reader);
        break;
    case X_FreePixmap:
        request_free_pixmap(server, client, &reader);
        break;
    case X_CreateGC:
        request_create_gc(server, client, &reader);
        break;
    case X_ChangeGC:
        request_change_gc(server, client, &reader);
        break;
    case X_CopyGC:
        request_copy_gc(server, client, &reader);
        break;
    case X_SetDashes:
        request_set_dashes(server, client, &reader);
        break;
    case X_SetClipRectangles:
        request_set_clip_rectangles(server, client, &reader);
        break;
    case X_FreeGC:
        request_free_gc(server, client, &reader);
        break;
    case X_ClearArea:
        request_clear_area(server, client, &reader);
        break;
    case X_CopyArea:
        request_copy_area(server, client, &reader);
        break;
    case X_PolyPoint:
        request_poly_point(server, client, &reader);
        break;
    case X_PolyLine:
        request_poly_line(server, client, &reader);
        break;
    case X_PolySegment:
        request_poly_segment(server, client, &reader);
        break;
    case X_PolyRectangle:
        request_poly_rectangle(server, client, &reader);
        break;
    case X_PolyArc:
    case X_PolyFillArc: {
        /* The server draws no arc, but the request is accepted. */
        uint32_t drawable = rd_u32(client, &reader);
        uint32_t gc_id = rd_u32(client, &reader);
        if (!find_gc(server, gc_id)) {
            x_reply_error(client, BadGC, 0, gc_id);
            break;
        }
        if (!drawable_exists(server, drawable)) {
            x_reply_error(client, BadDrawable, 0, drawable);
            break;
        }
        break;
    }
    case X_FillPoly:
        request_fill_poly(server, client, &reader);
        break;
    case X_PolyFillRectangle:
        request_poly_fill_rectangle(server, client, &reader);
        break;
    case X_PutImage:
        request_put_image(server, client, &reader);
        break;
    case X_GetImage:
        request_get_image(server, client, &reader);
        break;
    case X_PolyText8:
        request_poly_text(server, client, &reader, 0);
        break;
    case X_PolyText16:
        request_poly_text(server, client, &reader, 1);
        break;
    case X_ImageText8:
        request_image_text(server, client, &reader, 0);
        break;
    case X_ImageText16:
        request_image_text(server, client, &reader, 1);
        break;
    case X_CreateColormap:
        request_create_colormap(server, client, &reader);
        break;
    case X_FreeColormap:
        x_core_free_colormap(server, rd_u32(client, &reader));
        break;
    case X_CopyColormapAndFree:
        /* The request has no reply. */
        break;
    case X_InstallColormap:
    case X_UninstallColormap: {
        uint32_t colormap = rd_u32(client, &reader);
        (void)colormap;
        break;
    }
    case X_ListInstalledColormaps: {
        uint8_t buffer[32];
        reply_begin(client, buffer, 0U, 0U);
        x_reply_bytes(client, buffer, sizeof(buffer));
        break;
    }
    case X_AllocColor:
        request_alloc_color(server, client, &reader);
        break;
    case X_AllocNamedColor:
        request_alloc_named_color(server, client, &reader);
        break;
    case X_AllocColorCells:
    case X_AllocColorPlanes: {
        /* The reply reports that no pixel and no mask could be allocated. */
        uint8_t buffer[32];
        reply_begin(client, buffer, 0U, 0U);
        reply_put_u16(client, buffer, 8, 0U);
        reply_put_u16(client, buffer, 10, 0U);
        x_reply_bytes(client, buffer, sizeof(buffer));
        break;
    }
    case X_FreeColors:
        /* The request has no reply. */
        break;
    case X_StoreColors:
    case X_StoreNamedColor:
        /* The request has no reply. */
        break;
    case X_QueryColors:
        request_query_colors(server, client, &reader);
        break;
    case X_LookupColor:
        request_lookup_color(server, client, &reader);
        break;
    case X_CreateCursor:
        request_create_cursor(server, client, &reader);
        break;
    case X_CreateGlyphCursor:
        request_create_glyph_cursor(server, client, &reader);
        break;
    case X_FreeCursor:
        request_free_cursor(server, client, &reader);
        break;
    case X_RecolorCursor:
        /* The request has no reply. */
        break;
    case X_QueryBestSize: {
        uint8_t buffer[32];
        reply_begin(client, buffer, 0U, 0U);
        reply_put_u16(client, buffer, 8, 0U);
        reply_put_u16(client, buffer, 10, 0U);
        x_reply_bytes(client, buffer, sizeof(buffer));
        break;
    }
    case X_QueryExtension:
        request_query_extension(server, client, &reader);
        break;
    case X_ListExtensions:
        request_list_extensions(server, client, &reader);
        break;
    case X_ChangeKeyboardMapping:
    case X_ChangeKeyboardControl:
    case X_ChangePointerControl:
    case X_ChangeHosts:
    case X_SetAccessControl:
    case X_SetCloseDownMode:
    case X_KillClient:
    case X_RotateProperties:
    case X_SetScreenSaver:
        /* These requests have no reply, so the server accepts them and does
         * nothing.  A window manager that asks for them must not see a
         * protocol error, because a client treats an unexpected error as
         * fatal. */
        break;
    case X_GetKeyboardMapping:
        request_get_keyboard_mapping(server, client, &reader);
        break;
    case X_GetKeyboardControl:
        request_get_keyboard_control(server, client, &reader);
        break;
    case X_Bell:
        /* The request has no reply. */
        break;
    case X_GetPointerControl:
        request_get_pointer_control(server, client, &reader);
        break;
    case X_GetScreenSaver:
        request_get_screen_saver(server, client, &reader);
        break;
    case X_ListHosts: {
        /* The reply carries the host list, which is empty. */
        uint8_t buffer[32];
        reply_begin(client, buffer, 0U, 0U);
        x_reply_bytes(client, buffer, sizeof(buffer));
        break;
    }
    case X_SetPointerMapping: {
        /* The reply reports that the mapping was stored. */
        uint8_t buffer[32];
        reply_begin(client, buffer, 0U, 0U);
        x_reply_bytes(client, buffer, sizeof(buffer));
        break;
    }
    case X_GetPointerMapping:
        request_get_pointer_mapping(server, client, &reader);
        break;
    case X_SetModifierMapping: {
        /* The reply reports that the mapping was stored. */
        uint8_t buffer[32];
        reply_begin(client, buffer, 0U, 0U);
        x_reply_bytes(client, buffer, sizeof(buffer));
        break;
    }
    case X_GetModifierMapping:
        request_get_modifier_mapping(server, client, &reader);
        break;
    case X_NoOperation:
        /* The request has no reply. */
        break;
    case X_EXT_SHAPE_OPCODE:
        dispatch_shape_request(server, client, &reader);
        break;
    case X_EXT_XRES_OPCODE:
        dispatch_xres_request(server, client, &reader);
        break;
    default:
        x_reply_error(client, BadRequest, request->type, 0U);
        break;
    }
}

#endif
