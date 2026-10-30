/* Input handling for the akaOS X11 display server.
 *
 * The kernel publishes keyboard and pointer events on /dev/input/event0.
 * Opening that device also takes the keyboard away from the console line
 * discipline, so the display server becomes the sole keyboard consumer while
 * a graphical session is running.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <akaos/input.h>

#include "server.h"

/* Keysym values for the keys the server reports.  They match the X11
 * convention: Latin-1 characters use their code point and the function keys
 * live in the private range above 0xff00. */
#define XK_BackSpace 0xff08
#define XK_Tab 0xff09
#define XK_Return 0xff0d
#define XK_Escape 0xff1b
#define XK_BackSpace2 0xff08
#define XK_Home 0xff50
#define XK_Left 0xff51
#define XK_Up 0xff52
#define XK_Right 0xff53
#define XK_Down 0xff54
#define XK_Page_Up 0xff55
#define XK_Page_Down 0xff56
#define XK_End 0xff57
#define XK_Insert 0xff63
#define XK_Delete 0xffff
#define XK_KP_Enter 0xff8d
#define XK_KP_Multiply 0xffaa
#define XK_KP_Add 0xffab
#define XK_KP_Separator 0xffac
#define XK_KP_Subtract 0xffad
#define XK_KP_Decimal 0xffae
#define XK_KP_Divide 0xffaf
#define XK_KP_0 0xffb0
#define XK_KP_1 0xffb1
#define XK_KP_2 0xffb2
#define XK_KP_3 0xffb3
#define XK_KP_4 0xffb4
#define XK_KP_5 0xffb5
#define XK_KP_6 0xffb6
#define XK_KP_7 0xffb7
#define XK_KP_8 0xffb8
#define XK_KP_9 0xffb9
#define XK_F1 0xffbe
#define XK_F2 0xffbf
#define XK_F3 0xffc0
#define XK_F4 0xffc1
#define XK_F5 0xffc2
#define XK_F6 0xffc3
#define XK_F7 0xffc4
#define XK_F8 0xffc5
#define XK_F9 0xffc6
#define XK_F10 0xffc7
#define XK_F11 0xffc8
#define XK_F12 0xffc9
#define XK_Shift_L 0xffe1
#define XK_Shift_R 0xffe2
#define XK_Control_L 0xffe3
#define XK_Control_R 0xffe4
#define XK_Caps_Lock 0xffe5
#define XK_Shift_Lock 0xffe6
#define XK_Meta_L 0xffe7
#define XK_Meta_R 0xffe8
#define XK_Alt_L 0xffe9
#define XK_Alt_R 0xffea
#define XK_Super_L 0xffeb
#define XK_Super_R 0xffec
#define XK_Num_Lock 0xff7f
#define XK_Scroll_Lock 0xff14

struct key_entry {
    uint8_t scan;      /* kernel scan code */
    uint8_t xcode;     /* X11 key code */
    uint32_t keysym;
};

/* The kernel publishes PS/2 set 1 scan codes.  The table maps them onto the
 * conventional X11 key codes and their keysyms. */
static const struct key_entry key_table[] = {
    {1, 9, XK_Escape},
    {2, 10, '1'}, {3, 11, '2'}, {4, 12, '3'}, {5, 13, '4'}, {6, 14, '5'},
    {7, 15, '6'}, {8, 16, '7'}, {9, 17, '8'}, {10, 18, '9'}, {11, 19, '0'},
    {12, 20, '-'}, {13, 21, '='}, {14, 22, XK_BackSpace}, {15, 23, XK_Tab},
    {16, 24, 'q'}, {17, 25, 'w'}, {18, 26, 'e'}, {19, 27, 'r'}, {20, 28, 't'},
    {21, 29, 'y'}, {22, 30, 'u'}, {23, 31, 'i'}, {24, 32, 'o'}, {25, 33, 'p'},
    {26, 34, '['}, {27, 35, ']'}, {28, 36, XK_Return}, {29, 37, XK_Control_L},
    {30, 38, 'a'}, {31, 39, 's'}, {32, 40, 'd'}, {33, 41, 'f'}, {34, 42, 'g'},
    {35, 43, 'h'}, {36, 44, 'j'}, {37, 45, 'k'}, {38, 46, 'l'}, {39, 47, ';'},
    {40, 48, '\''}, {41, 49, '`'}, {42, 50, XK_Shift_L}, {43, 51, '\\'},
    {44, 52, 'z'}, {45, 53, 'x'}, {46, 54, 'c'}, {47, 55, 'v'}, {48, 56, 'b'},
    {49, 57, 'n'}, {50, 58, 'm'}, {51, 59, ','}, {52, 60, '.'}, {53, 61, '/'},
    {54, 62, XK_Shift_R}, {55, 63, XK_KP_Multiply}, {56, 64, XK_Alt_L},
    {57, 65, ' '}, {58, 66, XK_Caps_Lock}, {59, 67, XK_F1}, {60, 68, XK_F2},
    {61, 69, XK_F3}, {62, 70, XK_F4}, {63, 71, XK_F5}, {64, 72, XK_F6},
    {65, 73, XK_F7}, {66, 74, XK_F8}, {67, 75, XK_F9}, {68, 76, XK_F10},
    {69, 77, XK_Num_Lock}, {70, 78, XK_Scroll_Lock}, {71, 79, XK_KP_7},
    {72, 80, XK_KP_8}, {73, 81, XK_KP_9}, {74, 84, XK_KP_Subtract},
    {75, 82, XK_KP_4}, {76, 83, XK_KP_5}, {77, 84, XK_KP_6},
    {78, 85, XK_KP_Add}, {79, 87, XK_KP_1}, {80, 88, XK_KP_2},
    {81, 89, XK_KP_3}, {82, 90, XK_KP_0}, {83, 91, XK_KP_Decimal}
};

static const struct key_entry extended_key_table[] = {
    {0x48, 111, XK_Up},
    {0x50, 116, XK_Down},
    {0x4b, 110, XK_Left},
    {0x4d, 114, XK_Right},
    {0x47, 110, XK_Home},
    {0x4f, 115, XK_End},
    {0x49, 112, XK_Page_Up},
    {0x51, 117, XK_Page_Down},
    {0x52, 119, XK_Insert},
    {0x53, 118, XK_Delete},
    {103, 111, XK_Up},
    {108, 116, XK_Down},
    {105, 114, XK_Right},
    {106, 113, XK_Left}
};

static const struct key_entry *lookup_key(uint8_t scan, uint8_t *xcode) {
    for (size_t index = 0U; index < sizeof(key_table) / sizeof(key_table[0]);
         ++index) {
        if (key_table[index].scan != scan) continue;
        if (xcode) *xcode = key_table[index].xcode;
        return &key_table[index];
    }
    for (size_t index = 0U;
         index < sizeof(extended_key_table) / sizeof(extended_key_table[0]);
         ++index) {
        if (extended_key_table[index].scan != scan) continue;
        if (xcode) *xcode = extended_key_table[index].xcode;
        return &extended_key_table[index];
    }
    return NULL;
}

uint32_t x_keysym_for_keycode(struct x_server *server, uint32_t xcode) {
    for (size_t index = 0U; index < sizeof(key_table) / sizeof(key_table[0]);
         ++index) {
        if (key_table[index].xcode == xcode) return key_table[index].keysym;
    }
    for (size_t index = 0U;
         index < sizeof(extended_key_table) / sizeof(extended_key_table[0]);
         ++index) {
        if (extended_key_table[index].xcode == xcode) {
            return extended_key_table[index].keysym;
        }
    }
    (void)server;
    return 0U;
}

/* The modifier bits follow the X11 modifier mask layout. */
#define X_MOD_SHIFT 0x01U
#define X_MOD_LOCK 0x02U
#define X_MOD_CONTROL 0x04U
#define X_MOD_MOD1 0x08U
#define X_MOD_MOD2 0x10U
#define X_MOD_MOD3 0x20U

static uint32_t modifier_for_key(uint8_t xcode) {
    switch (xcode) {
    case 50: case 62: return X_MOD_SHIFT;
    case 37: case 105: return X_MOD_CONTROL;
    case 64: case 108: return X_MOD_MOD1;
    case 66: return X_MOD_LOCK;
    case 77: return X_MOD_MOD2;
    case 78: return X_MOD_MOD3;
    default: return 0U;
    }
}

static uint8_t xcode_for_scan(uint8_t scan) {
    uint8_t xcode = 0U;
    const struct key_entry *entry = lookup_key(scan, &xcode);
    (void)entry;
    return xcode;
}

void x_input_handle_key(struct x_server *server, uint16_t code, uint8_t value) {
    uint8_t xcode = xcode_for_scan((uint8_t)code);
    if (xcode == 0U) return;
    uint32_t modifier = modifier_for_key(xcode);
    if (value) {
        server->modifier_mask |= modifier;
        server->key_down[(xcode - 8U) / 8U] |=
            (uint8_t)(1U << ((xcode - 8U) % 8U));
    } else {
        server->modifier_mask &= ~modifier;
        server->key_down[(xcode - 8U) / 8U] &=
            (uint8_t)~(1U << ((xcode - 8U) % 8U));
    }
    x_event_key(server, (uint8_t)(value ? 1U : 0U), xcode);
}

void x_input_handle_button(struct x_server *server, uint16_t code, uint8_t value) {
    uint8_t button = 1U;
    if (code == AKAOS_BTN_RIGHT) button = 3U;
    else if (code == AKAOS_BTN_MIDDLE) button = 2U;
    if (value) {
        server->button_mask |= (uint32_t)(1U << (button - 1U));
    } else {
        server->button_mask &= ~(uint32_t)(1U << (button - 1U));
    }
    /* The pointer may have come to rest over a window that was mapped after
     * the last motion event. */
    server->pointer_window =
        x_window_find_at(server, server->pointer_x, server->pointer_y);
    x_event_button(server, (uint8_t)(value ? 1U : 0U), button, server->pointer_x,
                   server->pointer_y);
}

void x_input_handle_motion(struct x_server *server, int32_t dx, int32_t dy) {
    int32_t x = server->pointer_x + dx;
    int32_t y = server->pointer_y + dy;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= server->width) x = server->width - 1;
    if (y >= server->height) y = server->height - 1;
    server->pointer_x = (int16_t)x;
    server->pointer_y = (int16_t)y;
    server->pointer_window = x_window_find_at(server, server->pointer_x,
                                              server->pointer_y);
    x_event_motion(server, server->pointer_x, server->pointer_y);
}

void x_input_handle_absolute(struct x_server *server, int32_t x, int32_t y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= server->width) x = server->width - 1;
    if (y >= server->height) y = server->height - 1;
    server->pointer_x = (int16_t)x;
    server->pointer_y = (int16_t)y;
    server->pointer_window = x_window_find_at(server, server->pointer_x,
                                              server->pointer_y);
    x_event_motion(server, server->pointer_x, server->pointer_y);
}

void x_input_pump(struct x_server *server) {
    if (server->input_fd < 0) {
        server->input_fd = open(AKAOS_INPUT_EVENT_PATH, O_RDONLY);
        if (server->input_fd < 0) {
            printf("xserver: cannot open the input device: %s\n",
                   strerror(errno));
            fflush(stdout);
            return;
        }
        printf("xserver: input device opened on fd %d\n", server->input_fd);
        fflush(stdout);
    }
    /* Read the pending events in a single transfer.  The device blocks until
     * the next event arrives, so the pump must never loop on read(). */
    struct akaos_input_event events[16];
    ssize_t bytes = read(server->input_fd, events, sizeof(events));
    if (bytes <= 0) return;
    size_t count = (size_t)bytes / sizeof(events[0]);
    for (size_t index = 0U; index < count; ++index) {
        const struct akaos_input_event *event = &events[index];
        if (event->type == AKAOS_EV_KEY) {
            if (event->code >= AKAOS_BTN_LEFT) {
                x_input_handle_button(server, event->code,
                                      (uint8_t)(event->value ? 1U : 0U));
            } else {
                x_input_handle_key(server, event->code,
                                   (uint8_t)(event->value ? 1U : 0U));
            }
        } else if (event->type == AKAOS_EV_REL) {
            if (event->code == AKAOS_REL_X) {
                x_input_handle_motion(server, event->value, 0);
            } else if (event->code == AKAOS_REL_Y) {
                x_input_handle_motion(server, 0, event->value);
            }
        } else if (event->type == AKAOS_EV_ABS) {
            if (event->code == AKAOS_ABS_X) {
                x_input_handle_absolute(server, event->value, server->pointer_y);
            } else if (event->code == AKAOS_ABS_Y) {
                x_input_handle_absolute(server, server->pointer_x, event->value);
            }
        }
    }
}
