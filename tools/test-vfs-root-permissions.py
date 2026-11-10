#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression for root namespace authorization using production VFS paths."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'xscore/kernel/vfs.c').read_text()


def function(name):
    start = re.search(r'^(?:static )?[\w *]+\b' + name + r'\([^;{]*\)\s*\{', source, re.M).start()
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <akaos/fs.h>
#include <akaos/errno.h>
#define XSCORE_VFS_MAX_NODES 64
#define XSCORE_VFS_NAME_MAX AKAOS_NAME_MAX
#define XSCORE_VFS_DIRECT_BLOCKS 32
#define AKAOS_FS_SUPERBLOCK_A_SECTOR 8
'''
harness += source[source.index('enum xscore_vnode_type {'):
                  source.index('static struct xscore_vnode nodes')]
harness += source[source.index('#define XSCORE_VFS_PROC_PID_BASE'):
                  source.index('static void vfs_append_str')].replace(
                      'static uint32_t proc_usbinfo_node;\n', '').replace(
                      'static uint32_t proc_bus_usb_dev_node;\n', '')
harness += r'''
static struct xscore_vnode nodes[XSCORE_VFS_MAX_NODES];
static uint32_t node_count, persistence_generation;
static uint64_t active_superblock_sector;
static int vfs_initialized, vfs_locked, vfs_lock, held;
static int persistence_ready, persistence_loading;
static uint32_t uid, gid;
struct xscore_process { int unused; };
static struct xscore_process process;
static int xscore_process_pid_info(uint64_t pid, void *a, void *b, void *c) {
    (void)pid; (void)a; (void)b; (void)c; assert(0); return 0;
}
static struct xscore_process *xscore_process_current(void) { return &process; }
static uint32_t xscore_process_effective_uid(struct xscore_process *p) { (void)p; return uid; }
static uint32_t xscore_process_effective_gid(struct xscore_process *p) { (void)p; return gid; }
static uintptr_t xscore_spin_lock_irqsave(int *lock) { (void)lock; assert(!held); held = 1; return 0; }
static void vfs_lock_release(uintptr_t flags) { (void)flags; assert(held); held = 0; }
/* No file contents, hardware, or persistent disk are needed for directory tests. */
static void free_vnode_blocks(struct xscore_vnode *node) { (void)node; }
static int convert_to_dynamic(struct xscore_vnode *node) { (void)node; assert(0); return 0; }
static int xscore_vfs_persistence_sync(void) { assert(0); return 0; }
'''
for name in ['current_credentials', 'node_allows', 'name_equals', 'find_child',
             'split_path', 'xscore_vfs_resolve_path_at', 'xscore_vfs_initialize',
             'xscore_vfs_mkdir_at', 'xscore_vfs_rename_at']:
    harness += function(name)
harness += r'''
int main(void) {
    assert(xscore_vfs_initialize());
    assert(nodes[0].uid == 0 && nodes[0].gid == 0);
    assert(xscore_vfs_mkdir_at(0, "/etc", 0755) == 0);
    assert(xscore_vfs_mkdir_at(0, "/home", 0755) == 0);
    assert(xscore_vfs_mkdir_at(0, "/home/user", 0700) == 0);
    uint32_t home, etc, target;
    assert(xscore_vfs_resolve_path_at(0, "/home/user", &home) == 0);
    assert(xscore_vfs_resolve_path_at(0, "/etc", &etc) == 0);
    nodes[home].uid = 1000; nodes[home].gid = 1000;
    for (int group = 0; group < 2; ++group) {
        uid = 1000; gid = group ? 0 : 1000;
        assert(xscore_vfs_resolve_path_at(0, "/etc", &target) == 0 && target == etc);
        assert(xscore_vfs_rename_at(0, "/etc", 0, "/etc.old") == -AKAOS_EACCES);
        assert(xscore_vfs_rename_at(0, "/etc", home, "stolen") == -AKAOS_EACCES);
        assert(xscore_vfs_mkdir_at(0, "/new", 0755) == -AKAOS_EACCES);
        assert(xscore_vfs_mkdir_at(0, "/home/user/new", 0700) == 0);
        assert(xscore_vfs_rename_at(home, "new", 0, "/etc") == -AKAOS_EACCES);
        assert(xscore_vfs_rename_at(home, "new", 0, "/new") == -AKAOS_EACCES);
        assert(xscore_vfs_rename_at(home, "new", home, group ? "second" : "first") == 0);
        assert(!held);
    }
    uid = gid = 0;
    assert(xscore_vfs_rename_at(0, "/etc", 0, "/etc.old") == 0);
    assert(xscore_vfs_mkdir_at(0, "/etc", 0755) == 0);
    assert(xscore_vfs_resolve_path_at(0, "/etc.old", &target) == 0 && target == etc);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-root-permissions-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(ROOT / 'sdk/include'),
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('VFS root namespace authorization regression passed')
