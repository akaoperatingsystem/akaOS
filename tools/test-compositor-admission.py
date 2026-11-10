#!/usr/bin/env python3
# Copyright (c) 2026 akaOS Authors
# SPDX-License-Identifier: BSD-3-Clause
"""Check compositor object admission before any shared input is mapped."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
toolkit = (ROOT / 'userspace/libc/src/toolkit.c').read_text()
connect = toolkit[toolkit.index('int akaos_toolkit_connect('):
                  toolkit.index('int akaos_toolkit_display_info(')]
lumen = (ROOT / 'userspace/bin/lumen/main.c').read_text()
# Both self-test and production must use fresh, private objects.
assert lumen.count('int ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH,\n'
                   '                          O_CREAT | O_EXCL | O_RDWR, 0600);') == 2
assert 'shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_CREAT | O_RDWR' not in lumen
source = r'''
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <akaos/display.h>
static struct akaos_compositor_channel channel;
static struct stat metadata;
static int stat_error, maps, closes;
static int mock_shm_open(const char *name, int flags, mode_t mode) {
    (void)name; assert(flags == O_RDWR && mode == 0); return 42;
}
static int mock_fstat(int fd, struct stat *status) {
    assert(fd == 42); *status = metadata; return stat_error;
}
static int mock_close(int fd) { assert(fd == 42); ++closes; return 0; }
static void *mock_mmap(void *a, size_t n, int p, int f, int fd, off_t o) {
    (void)a; (void)p; (void)f; (void)o;
    assert(fd == 42 && n == sizeof(channel)); ++maps; return &channel;
}
static int mock_munmap(void *a, size_t n) { (void)a; (void)n; return 0; }
#define shm_open mock_shm_open
#define fstat mock_fstat
#define close mock_close
#define mmap mock_mmap
#define munmap mock_munmap
'''
kernel = (ROOT / 'xscore/kernel/process.c').read_text()
allows = kernel[kernel.index('static int shm_object_allows('):
                kernel.index('int64_t xscore_process_shm_open(')]
source += r'''
struct process_shm_object { uint32_t owner_uid, owner_gid, mode; };
struct xscore_process { uint32_t effective_uid, effective_gid; };
'''
source += allows
source += connect
source += r'''
int main(void) {
    struct process_shm_object object = {0, 0, 0600};
    struct xscore_process process = {1000, 0};
    assert(!shm_object_allows(&object, &process, 1, 0));
    assert(!shm_object_allows(&object, &process, 0, 1));
    process.effective_gid = 1000;
    assert(!shm_object_allows(&object, &process, 1, 1));
    process.effective_uid = 0;
    assert(shm_object_allows(&object, &process, 1, 1));
    struct akaos_compositor_channel *result;
    channel.abi_version = AKAOS_DISPLAY_ABI_VERSION;
    channel.protocol_version = AKAOS_COMPOSITOR_PROTOCOL_VERSION;
    metadata.st_mode = 0100600;
    metadata.st_size = sizeof(channel);
    assert(akaos_toolkit_connect(&result) == 0 && result == &channel);
    assert(maps == 1 && closes == 1);
    for (unsigned i = 0; i < 5; ++i) {
        maps = closes = 0;
        metadata.st_uid = i == 0 ? 1000 : 0;
        metadata.st_mode = i == 1 ? 0100666 : i == 2 ? 0100644 : 0100600;
        metadata.st_size = i == 3 ? 0 : sizeof(channel);
        stat_error = i == 4 ? -1 : 0;
        result = &channel;
        assert(akaos_toolkit_connect(&result) != 0 && result == 0);
        assert(maps == 0 && closes == 1);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-compositor-admission-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(source)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-Wall', '-Wextra',
                    '-Werror', '-I' + str(ROOT / 'sdk/include'),
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('[PASS] Compositor admission rejects untrusted objects before mapping')
