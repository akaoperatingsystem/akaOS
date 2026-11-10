#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression for device open permission checks using production helpers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = source.index(name + '(')
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


syscall = (ROOT / 'xscore/kernel/syscall.c').read_text()
vfs = (ROOT / 'xscore/kernel/vfs.c').read_text()
# Both syscall entry points must gate all device branches before dispatch.
for cwd, flags in [('cwd_node', 'argument2'), ('open_cwd', 'args.flags')]:
    start = syscall.index(f'xscore_vfs_device_kind_at({cwd}, path);')
    end = syscall.index('return (uint64_t)xscore_vfs_open_at', start)
    dispatch = syscall[start:end]
    gate = dispatch.index(f'authorize_device_open({cwd}, path, {flags})')
    assert 'if (device != XSCORE_DEVICE_NONE)' in dispatch[:gate]
    assert dispatch.index('if (error != 0) return (uint64_t)(int64_t)error;') < dispatch.index('open_character_device(')

harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <akaos/fs.h>
#include <akaos/errno.h>
struct xscore_process { int unused; };
static struct xscore_process process;
static uint32_t uid, gid;
static struct xscore_process *xscore_process_current(void) { return &process; }
static uint32_t xscore_process_effective_uid(struct xscore_process *p) { (void)p; return uid; }
static uint32_t xscore_process_effective_gid(struct xscore_process *p) { (void)p; return gid; }
struct xscore_vnode { uint32_t uid, gid, mode; };
static struct xscore_vnode nodes[1];
static uint32_t expected_cwd;
static const char *expected_path;
static int resolution_error;
static int xscore_vfs_resolve_path_at(uint32_t cwd, const char *path, uint32_t *target) {
    assert(cwd == expected_cwd && strcmp(path, expected_path) == 0);
    *target = 0;
    return resolution_error;
}
'''
for name in ['current_credentials', 'node_allows', 'xscore_vfs_access_at']:
    harness += function(vfs, name)
harness += function(syscall, 'authorize_device_open')
harness += r'''
int main(void) {
    nodes[0] = (struct xscore_vnode){0, 0, 0620};
    const char *paths[] = {"/dev/tty1", "tty1", "/console-link"};
    for (unsigned p = 0; p < 3; ++p) {
        expected_path = paths[p]; expected_cwd = p == 1 ? 7 : 0;
        uid = 1000; gid = 1000;
        for (unsigned flags = 0; flags <= 2; ++flags)
            assert(authorize_device_open(expected_cwd, paths[p], flags) == -AKAOS_EACCES);
        gid = 0;
        assert(authorize_device_open(expected_cwd, paths[p], AKAOS_O_WRONLY) == 0);
        assert(authorize_device_open(expected_cwd, paths[p], AKAOS_O_RDONLY) == -AKAOS_EACCES);
        assert(authorize_device_open(expected_cwd, paths[p], AKAOS_O_RDWR) == -AKAOS_EACCES);
        uid = 0;
        for (unsigned flags = 0; flags <= 2; ++flags)
            assert(authorize_device_open(expected_cwd, paths[p], flags) == 0);
    }
    uid = 1000; gid = 1000; nodes[0].uid = 1000; nodes[0].mode = 0400;
    assert(authorize_device_open(expected_cwd, expected_path, AKAOS_O_RDONLY) == 0);
    assert(authorize_device_open(expected_cwd, expected_path, AKAOS_O_WRONLY) == -AKAOS_EACCES);
    nodes[0].uid = 0; nodes[0].mode = 0666;
    for (unsigned flags = 0; flags <= 2; ++flags)
        assert(authorize_device_open(expected_cwd, expected_path, flags) == 0);
    assert(authorize_device_open(expected_cwd, expected_path, 3) == -AKAOS_EINVAL);
    resolution_error = -AKAOS_ENOENT;
    assert(authorize_device_open(expected_cwd, expected_path, 0) == -AKAOS_ENOENT);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-device-') as tmp:
    source, binary = Path(tmp) / 'test.c', Path(tmp) / 'test'
    source.write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-I', str(ROOT / 'sdk/include'),
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Device authorization regression passed')
