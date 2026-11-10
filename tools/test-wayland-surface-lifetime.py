#!/usr/bin/env python3
# Copyright (c) 2026 akaOS Authors
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression for bridge destruction callbacks (requires libwayland-dev)."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
source = (ROOT / 'userspace/bin/wayland-bridge/main.c').read_text()


def function(name):
    start = source.index('static void ' + name + '(')
    end = source.index('\n}\n', start) + 3
    return source[start:end]


# Compile the actual state layout and handlers, with display operations stubbed.
state = source[source.index('struct bridge_layer_state {'):
               source.index('struct bridge_foreign_manager {')]
prelude = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <akaos/toolkit.h>
#define WAYLAND_SURFACE_LIMIT 8
#define WAYLAND_SEAT_LIMIT 2
struct bridge;
struct bridge_foreign_handle;
'''
stubs = r'''
struct bridge_seat {
    struct bridge_surface *cursor_surface;
    struct wl_resource *focused_surface, *keyboard_focus_surface, *popup_grab_origin;
};
struct bridge {
    struct bridge_surface *surfaces[WAYLAND_SURFACE_LIMIT];
    struct bridge_surface *grabbed_popup, *moving_surface, *resizing_surface;
    struct bridge_seat *moving_seat, *resizing_seat;
    struct bridge_seat seats[WAYLAND_SEAT_LIMIT];
};
static void unmap_surface(struct bridge_surface *s) { s->attached = 0; }
static void restore_popup_focus(struct bridge_surface *s) { (void)s; }
static void foreign_toplevel_unmap(struct bridge_surface *s) { (void)s; }
static void foreign_toplevel_send_metadata(struct bridge_surface *s) { (void)s; }
static void copy_text(char *dst, size_t size, const char *src) {
    snprintf(dst, size, "%s", src);
}
#define XDG_SURFACE_ERROR_INVALID_SIZE 5
#define XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT 6
#define XDG_WM_BASE_ERROR_ROLE 0
#define XDG_WM_BASE_ERROR_INVALID_SURFACE_STATE 4
#define xdg_surface_interface wl_surface_interface
static const int xdg_surface_implementation = 0;
'''
functions = '\n'.join(function(name) for name in (
    'resource_destroy', 'surface_destroyed', 'xdg_surface_destroyed',
    'toplevel_resource_destroyed', 'xdg_popup_resource_destroyed', 'xdg_surface_geometry',
    'xdg_surface_ack_configure', 'toplevel_title', 'toplevel_app_id',
    'xdg_surface_destroy', 'wm_base_get_xdg_surface'))
tests = r'''
static struct wl_resource *resource(struct wl_client *client,
    struct bridge_surface *s, wl_resource_destroy_func_t destroy) {
    struct wl_resource *r = wl_resource_create(client, &wl_surface_interface, 1, 0);
    assert(r);
    wl_resource_set_implementation(r, NULL, s, destroy);
    return r;
}
int main(void) {
    for (int order = 0; order < 6; ++order) {
        struct wl_display *display = wl_display_create();
        int sockets[2];
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        struct wl_client *client = wl_client_create(display, sockets[0]);
        assert(client);
        struct bridge bridge = {0};
        struct bridge_surface *s = calloc(1, sizeof(*s));
        struct bridge_surface child = {0};
        s->bridge = &bridge;
        s->wl_surface = resource(client, s, surface_destroyed);
        s->xdg_surface = resource(client, s, xdg_surface_destroyed);
        s->toplevel = resource(client, s, toplevel_resource_destroyed);
        s->popup = resource(client, s, xdg_popup_resource_destroyed);
        struct wl_resource *wl = s->wl_surface, *xdg = s->xdg_surface;
        struct wl_resource *top = s->toplevel, *popup = s->popup;
        bridge.surfaces[0] = s;
        bridge.surfaces[1] = &child;
        bridge.grabbed_popup = s;
        bridge.moving_surface = s;
        bridge.resizing_surface = s;
        bridge.seats[0].popup_grab_origin = wl;
        bridge.seats[0].cursor_surface = s;
        bridge.seats[0].focused_surface = wl;
        bridge.seats[0].keyboard_focus_surface = wl;
        child.popup_parent = s;
        child.attached = 1;
        xdg_surface_geometry(client, xdg, 10, 20, 100, 200);
        toplevel_title(client, top, "live title");
        assert(s->window_geometry_x == 10 && !strcmp(s->title, "live title"));
        if (order == 0) {
            /* Reject duplicate objects before they can hide a live reference. */
            struct wl_resource *base = resource(client, NULL, NULL);
            wm_base_get_xdg_surface(client, base, 10, wl);
            assert(s->xdg_surface == xdg);
            /* A live role must prevent xdg_surface destruction/recreation. */
            xdg_surface_destroy(client, xdg);
            assert(s->xdg_surface == xdg);
        }
        if (order == 1) { wl_resource_destroy(top); top = NULL; assert(!s->toplevel); }
        if (order == 2) { wl_resource_destroy(xdg); xdg = NULL; }
        if (order == 3) { wl_client_destroy(client); client = NULL; }
        else wl_resource_destroy(wl);
        assert(!bridge.surfaces[0] && !child.popup_parent && !child.attached);
        assert(!bridge.grabbed_popup && !bridge.moving_surface && !bridge.resizing_surface);
        assert(!bridge.seats[0].cursor_surface && !bridge.seats[0].focused_surface);
        assert(!bridge.seats[0].keyboard_focus_surface);
        assert(!bridge.seats[0].popup_grab_origin);
        if (client) {
            if (xdg) {
                assert(!wl_resource_get_user_data(xdg));
                xdg_surface_geometry(client, xdg, INT32_MAX, INT32_MIN, 99, 99);
                xdg_surface_ack_configure(client, xdg, 0);
            }
            if (top) {
                assert(!wl_resource_get_user_data(top));
                toplevel_title(client, top, "after free");
                toplevel_app_id(client, top, "after free");
            }
            assert(!wl_resource_get_user_data(popup));
            if (order == 4 && xdg) { wl_resource_destroy(xdg); xdg = NULL; }
            if (order == 5 && top) { wl_resource_destroy(top); top = NULL; }
            wl_client_destroy(client);
        }
        close(sockets[1]);
        wl_display_destroy(display);
    }
    puts("Wayland surface lifetime regression passed");
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-wayland-lifetime-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(prelude + state + stubs + functions + tests)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-D_GNU_SOURCE',
                    '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(ROOT / 'sdk/include'), str(path / 'test.c'),
                    '-lwayland-server', '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
