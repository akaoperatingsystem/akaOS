#!/usr/bin/env python3
# Copyright (c) 2026 akaOS Authors
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression for popup grab authorization (requires libwayland-dev)."""
import os
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
source = (ROOT / 'userspace/bin/wayland-bridge/main.c').read_text()


def function(name):
    # Match definitions rather than forward declarations.
    match = re.search(r'static (?:void|int) ' + name + r'\([^;]*?\) \{', source)
    start = match.start()
    end = source.index('\n}\n', start) + 3
    return source[start:end]


seat = source[source.index('struct bridge_seat {'):
              source.index('struct bridge_positioner {')]
state = source[source.index('struct bridge_layer_state {'):
               source.index('struct bridge_foreign_manager {')]
prelude = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <akaos/toolkit.h>
#define WAYLAND_SEAT_LIMIT 2
#define WAYLAND_SURFACE_LIMIT 8
#define XDG_POPUP_ERROR_INVALID_GRAB 0
struct bridge;
struct bridge_surface;
struct bridge_foreign_handle;
'''
stubs = r'''
struct bridge {
    struct bridge_seat seats[WAYLAND_SEAT_LIMIT];
    struct bridge_surface *grabbed_popup, *active;
};
static int errors, done;
static void post_error(struct wl_resource *r, uint32_t code, const char *fmt, ...) {
    (void)r; (void)code; (void)fmt; ++errors;
}
#define wl_resource_post_error post_error
static void xdg_popup_send_popup_done(struct wl_resource *r) { (void)r; ++done; }
static struct bridge_surface *active_wayland_surface(struct bridge *b, void *s) {
    (void)s; return b->active;
}
static void send_keyboard_focus(struct bridge *b, struct bridge_seat *s,
                                struct bridge_surface *target) {
    (void)b; s->keyboard_focus_surface = target ? target->wl_surface : NULL;
}
static void foreign_toplevel_unmap(struct bridge_surface *s) { (void)s; }
static int test_surface_destroy(struct akaos_toolkit_surface *s) { (void)s; return 0; }
#define akaos_toolkit_surface_destroy test_surface_destroy
'''
functions = '\n'.join(function(name) for name in (
    'surface_is_within_popup', 'unmap_surface', 'xdg_popup_grab', 'restore_popup_focus',
    'dismiss_grabbed_popup_to'))
release_start = source.index('            seat->pressed_button = 0U;',
                             source.index('static void deliver_wayland_pointer_event('))
release_end = source.index('            if (bridge->moving_seat', release_start)
functions += ('\nstatic void release_press(struct bridge *bridge, '
              'struct bridge_seat *seat) {\n' +
              source[release_start:release_end] + '}\n')
tests = r'''
static struct wl_resource *resource(struct wl_client *client, void *data) {
    struct wl_resource *r = wl_resource_create(client, &wl_surface_interface, 1, 0);
    assert(r);
    wl_resource_set_user_data(r, data);
    return r;
}
int main(void) {
    struct wl_display *display = wl_display_create();
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct wl_client *client = wl_client_create(display, sockets[0]);
    assert(client);
    struct bridge b = {0};
    struct bridge_surface root = {.bridge = &b, .attached = 1};
    struct bridge_surface other = {.bridge = &b, .attached = 1};
    struct bridge_surface sub = {.bridge = &b, .attached = 1, .subsurface_parent = &root};
    struct bridge_surface popup = {.bridge = &b, .popup_parent = &root};
    struct bridge_surface nested = {.bridge = &b, .popup_parent = &popup};
    root.wl_surface = resource(client, &root);
    other.wl_surface = resource(client, &other);
    sub.wl_surface = resource(client, &sub);
    popup.wl_surface = resource(client, &popup);
    popup.popup = resource(client, &popup);
    nested.wl_surface = resource(client, &nested);
    nested.popup = resource(client, &nested);
    struct bridge_seat *seat = &b.seats[0];
    seat->bridge = &b;
    seat->client = client;
    seat->resource = resource(client, seat);
    seat->last_pointer_press_serial = 42;
    seat->popup_grab_origin = root.wl_surface;
    b.active = &root;
    seat->pressed_button = 1;
    release_press(&b, seat);
    assert(!seat->popup_grab_origin);
    seat->popup_grab_origin = root.wl_surface;
    /* A released press cannot start a new grab. */
    xdg_popup_grab(client, popup.popup, seat->resource, 42);
    assert(!b.grabbed_popup && done == 1);
    seat->pressed_button = 1;
    /* Current compositor focus is checked even before another input event. */
    b.active = &other;
    xdg_popup_grab(client, popup.popup, seat->resource, 42);
    assert(!b.grabbed_popup && done == 2);
    b.active = &root;
    seat->popup_grab_origin = other.wl_surface;
    xdg_popup_grab(client, popup.popup, seat->resource, 42);
    assert(!b.grabbed_popup && done == 3);
    seat->popup_grab_origin = sub.wl_surface;
    xdg_popup_grab(client, popup.popup, seat->resource, 41);
    assert(!b.grabbed_popup && errors == 1);
    xdg_popup_grab(client, popup.popup, seat->resource, 42);
    assert(b.grabbed_popup == &popup);
    assert(seat->keyboard_focus_surface == popup.wl_surface);
    popup.attached = 1;
    release_press(&b, seat);
    assert(seat->popup_grab_origin);
    /* The original interaction can extend an installed stack after release. */
    xdg_popup_grab(client, nested.popup, seat->resource, 42);
    assert(b.grabbed_popup == &nested);
    nested.attached = 1;
    restore_popup_focus(&nested);
    assert(b.grabbed_popup == &popup && seat->popup_grab_origin);
    dismiss_grabbed_popup_to(&b, NULL);
    assert(!b.grabbed_popup && !seat->popup_grab_origin);
    popup.attached = 0;
    seat->pressed_button = 1;
    xdg_popup_grab(client, popup.popup, seat->resource, 42);
    assert(!b.grabbed_popup && errors == 2);
    /* Destroying the last grabbing popup also revokes its authorization. */
    seat->popup_grab_origin = root.wl_surface;
    b.grabbed_popup = &popup;
    restore_popup_focus(&popup);
    assert(!b.grabbed_popup && !seat->popup_grab_origin);
    seat->popup_grab_origin = sub.wl_surface;
    unmap_surface(&root);
    assert(!seat->popup_grab_origin);
    wl_client_destroy(client);
    close(sockets[1]);
    wl_display_destroy(display);
    puts("Wayland popup grab regression passed");
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-wayland-popup-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(prelude + seat + state + stubs + functions + tests)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-D_GNU_SOURCE',
                    '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(ROOT / 'sdk/include'), str(path / 'test.c'),
                    '-lwayland-server', '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
