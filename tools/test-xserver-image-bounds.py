#!/usr/bin/env python3
# Copyright (c) 2026 akaOS Authors
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise PutImage's request length boundary and real renderer on the host."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
XSERVER = ROOT / 'userspace/bin/xserver'
dispatch = (XSERVER / 'dispatch.inc').read_text()
handler = dispatch[dispatch.index('static void request_put_image('):
                   dispatch.index('static void request_get_image(')]
SOURCE = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "server.h"
static struct x_gc gc;
static int error;
static struct x_gc *find_gc(struct x_server *s, uint32_t id) {
    (void)s; return id == 2 ? &gc : NULL;
}
static int drawable_exists(struct x_server *s, uint32_t id) {
    return id == s->pixmaps[0].id;
}
static int resolve_window(struct x_server *s, uint32_t id) {
    return x_window_index(s, id);
}
void x_reply_error(struct x_client *c, uint8_t e, uint8_t minor, uint32_t value) {
    (void)c; (void)minor; (void)value; error = e;
}
void x_server_damage(struct x_server *s, int i, int16_t x, int16_t y,
                     int16_t w, int16_t h) {
    (void)s; (void)i; (void)x; (void)y; (void)w; (void)h;
}
void x_server_repaint(struct x_server *s) { (void)s; }
static uint32_t rd_u32(struct x_client *c, struct x_reader *r) {
    (void)c; return x_reader_u32(r);
}
static uint16_t rd_u16(struct x_client *c, struct x_reader *r) {
    (void)c; return x_reader_u16(r);
}
static int16_t rd_i16(struct x_client *c, struct x_reader *r) {
    return (int16_t)rd_u16(c, r);
}
static uint8_t rd_u8(struct x_reader *r) { return x_reader_u8(r); }
'''
SOURCE += handler
SOURCE += r'''
int main(void) {
    struct x_server *s = calloc(1, sizeof(*s));
    struct x_client client = {0};
    uint32_t target[8];
    s->pixmaps[0].id = 1;
    s->pixmaps[0].width = 4;
    s->pixmaps[0].height = 2;
    s->pixmaps[0].pixels = target;
    /* Exact request allocations make ASan detect the original heap overread. */
    const unsigned depths[] = {0, 1, 4, 8, 16, 24, 32, 255};
    for (unsigned d = 0; d < sizeof(depths) / sizeof(depths[0]); ++d) {
        size_t bytes = depths[d] <= 8 ? 4 : depths[d] <= 16 ? 8 : 16;
        uint8_t *body = calloc(1, 20 + bytes);
        body[0] = 1; body[4] = 2; body[8] = 4; body[10] = 1;
        body[17] = depths[d];
        memset(body + 20, 0x5a, bytes);
        memset(target, 0, sizeof(target));
        struct x_reader r;
        x_reader_init(&r, body, 20 + bytes);
        client.request_minor = 2;
        error = 0;
        request_put_image(s, &client, &r);
        assert(error == 0);
        for (unsigned i = 0; i < 4; ++i)
            assert(target[i] == (i < bytes / 4 ? 0x5a5a5a5aU : 0U));
        x_reader_init(&r, body, 20 + bytes - 1);
        memset(target, 0, sizeof(target));
        request_put_image(s, &client, &r);
        assert(error == BadLength);
        for (unsigned i = 0; i < 8; ++i) assert(target[i] == 0);
        free(body);
    }
    /* Independently test short buffers for all renderer paths and clipping. */
    for (unsigned format = 0; format < 3; ++format) {
        for (size_t length = 0; length <= 32; ++length) {
            uint8_t *source = malloc(length ? length : 1);
            memset(source, 0x5a, length);
            for (int x = -2; x <= 2; ++x)
                x_drawable_image(s, 1, x, 0, 4, 2, 24, format, source, length);
            free(source);
        }
    }
    free(s);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-ximage-') as directory:
    source = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    source.write_text(SOURCE)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-I' + str(XSERVER),
                    '-I' + str(XSERVER / 'include'), '-I' + str(ROOT / 'sdk/include'),
                    str(source), str(XSERVER / 'draw.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('[PASS] PutImage and renderer source bounds under ASan/UBSan')
