/* Atoms, window properties and colour helpers. */
#include <stdlib.h>
#include <string.h>

#include "server.h"

/* The predefined atoms occupy the first identifiers in the atom table. */
static const char *predefined_atoms[] = {
    "PRIMARY", "SECONDARY", "ARC", "ATOM", "BITMAP", "CARDINAL", "COLORMAP",
    "CURSOR", "CUT_BUFFER0", "CUT_BUFFER1", "CUT_BUFFER2", "CUT_BUFFER3",
    "CUT_BUFFER4", "CUT_BUFFER5", "CUT_BUFFER6", "CUT_BUFFER7", "DRAWABLE",
    "FONT", "INTEGER", "PIXMAP", "POINT", "RECTANGLE", "RESOURCE_MANAGER",
    "RGB_COLOR_MAP", "RGB_BEST_MAP", "RGB_BLUE_MAP", "RGB_DEFAULT_MAP",
    "RGB_GRAY_MAP", "RGB_GREEN_MAP", "RGB_RED_MAP", "STRING", "VISUALID",
    "WINDOW", "WM_COMMAND", "WM_HINTS", "WM_CLIENT_MACHINE", "WM_ICON_NAME",
    "WM_ICON_SIZE", "WM_NAME", "WM_NORMAL_HINTS", "WM_SIZE_HINTS", "WM_ZOOM_HINTS",
    "MIN_SPACE", "NORM_SPACE", "MAX_SPACE", "END_SPACE", "SUPERSCRIPT_X",
    "SUPERSCRIPT_Y", "SUBSCRIPT_X", "SUBSCRIPT_Y", "UNDERLINE_POSITION",
    "UNDERLINE_THICKNESS", "STRIKEOUT_ASCENT", "STRIKEOUT_DESCENT",
    "ITALIC_ANGLE", "X_HEIGHT", "QUAD_WIDTH", "WEIGHT", "POINT_SIZE",
    "RESOLUTION", "COPYRIGHT", "NOTICE", "FONT_NAME", "FAMILY_NAME",
    "FULL_NAME", "CAP_HEIGHT", "WM_CLASS", "WM_TRANSIENT_FOR"
};

static void install_predefined_atoms(struct x_server *server) {
    for (size_t index = 0U;
         index < sizeof(predefined_atoms) / sizeof(predefined_atoms[0]) &&
         index < X_MAX_ATOMS;
         ++index) {
        struct x_atom *atom = &server->atoms[index];
        atom->id = (uint32_t)(index + 1U);
        size_t length = strlen(predefined_atoms[index]);
        if (length >= sizeof(atom->name)) length = sizeof(atom->name) - 1U;
        memcpy(atom->name, predefined_atoms[index], length);
        atom->name[length] = '\0';
        atom->name_length = length;
    }
    server->atom_count = (int)(sizeof(predefined_atoms) /
                               sizeof(predefined_atoms[0]));
    if (server->atom_count > X_MAX_ATOMS) server->atom_count = X_MAX_ATOMS;
    server->next_atom = (uint32_t)server->atom_count + 1U;
}

void x_server_install_atoms(struct x_server *server) {
    install_predefined_atoms(server);
}

uint32_t x_atom_intern(struct x_server *server, const char *name, size_t length,
                       int only_if_exists) {
    if (!name || length == 0U) return 0U;
    for (int index = 0; index < server->atom_count; ++index) {
        struct x_atom *atom = &server->atoms[index];
        if (atom->name_length == length &&
            memcmp(atom->name, name, length) == 0) {
            return atom->id;
        }
    }
    if (only_if_exists) return 0U;
    if (server->atom_count >= X_MAX_ATOMS) return 0U;
    struct x_atom *atom = &server->atoms[server->atom_count];
    if (length >= sizeof(atom->name)) length = sizeof(atom->name) - 1U;
    memcpy(atom->name, name, length);
    atom->name[length] = '\0';
    atom->name_length = length;
    atom->id = server->next_atom++;
    ++server->atom_count;
    return atom->id;
}

const char *x_atom_name(struct x_server *server, uint32_t atom,
                         size_t *length) {
    for (int index = 0; index < server->atom_count; ++index) {
        struct x_atom *entry = &server->atoms[index];
        if (entry->id != atom) continue;
        if (length) *length = entry->name_length;
        return entry->name;
    }
    return NULL;
}

/* -------------------------------------------------------------- properties */
static int find_property(struct x_server *server, xid_t window, uint32_t atom) {
    for (int index = 0; index < X_MAX_PROPERTIES; ++index) {
        struct x_property *property = &server->properties[index];
        if (property->window == window && property->atom == atom &&
            property->data) {
            return index;
        }
    }
    return -1;
}

int x_property_set(struct x_server *server, xid_t window, uint32_t atom,
                   uint32_t type, uint8_t format, const void *data,
                   uint32_t item_count) {
    size_t unit = (size_t)format / 8U;
    size_t bytes = (size_t)item_count * unit;
    int index = find_property(server, window, atom);
    if (index < 0) {
        for (int candidate = 0; candidate < X_MAX_PROPERTIES; ++candidate) {
            if (server->properties[candidate].data) continue;
            index = candidate;
            break;
        }
        if (index < 0) return 0;
        server->properties[index].window = window;
        server->properties[index].atom = atom;
        server->properties[index].data = NULL;
    }
    struct x_property *property = &server->properties[index];
    if (bytes) {
        uint8_t *storage = realloc(property->data, bytes);
        if (!storage) return 0;
        memcpy(storage, data, bytes);
        property->data = storage;
    } else if (property->data) {
        free(property->data);
        property->data = NULL;
    }
    property->type = type;
    property->format = format;
    property->item_count = item_count;
    property->bytes = bytes;
    return 1;
}

int x_property_get(struct x_server *server, xid_t window, uint32_t atom,
                   uint32_t *type, uint8_t *format, uint32_t *item_count,
                   const uint8_t **data) {
    int index = find_property(server, window, atom);
    if (index < 0) return 0;
    struct x_property *property = &server->properties[index];
    if (type) *type = property->type;
    if (format) *format = property->format;
    if (item_count) *item_count = property->item_count;
    if (data) *data = property->data;
    return 1;
}

int x_property_delete(struct x_server *server, xid_t window, uint32_t atom) {
    int index = find_property(server, window, atom);
    if (index < 0) return 0;
    struct x_property *property = &server->properties[index];
    if (property->data) free(property->data);
    memset(property, 0, sizeof(*property));
    return 1;
}

int x_property_list(struct x_server *server, xid_t window, uint32_t *atoms,
                    uint32_t max_count) {
    uint32_t count = 0U;
    for (int index = 0; index < X_MAX_PROPERTIES; ++index) {
        struct x_property *property = &server->properties[index];
        if (!property->data || property->window != window) continue;
        if (count >= max_count) break;
        atoms[count++] = property->atom;
    }
    return (int)count;
}
