#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression for the production PTY allocation and authorization boundary."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = source.index(name + '(')
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


vfs = (ROOT / 'xscore/kernel/vfs.c').read_text()
files = (ROOT / 'xscore/kernel/file.c').read_text()
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <akaos/errno.h>
#include <akaos/fs.h>
#define XSCORE_VFS_MAX_NODES 16
#define XSCORE_VNODE_CHARACTER 3
#define XSCORE_PTY_MAX 4
#define FILE_READABLE 1
#define FILE_WRITABLE 2
#define FILE_PTY_MASTER 4
#define FILE_PTY_SLAVE 8
struct xscore_process { int unused; };
static uint32_t uid = 1000, gid = 100;
static struct xscore_process process;
static struct xscore_process *xscore_process_current(void) { return &process; }
static uint32_t xscore_process_effective_uid(struct xscore_process *p) { (void)p; return uid; }
static uint32_t xscore_process_effective_gid(struct xscore_process *p) { (void)p; return gid; }
struct xscore_vnode { uint32_t type, uid, gid, mode, references; int unlinked; };
static struct xscore_vnode nodes[XSCORE_VFS_MAX_NODES];
static int vfs_locked = 1, vfs_lock, tree_held, pool_held;
static uintptr_t xscore_spin_lock_irqsave(int *lock) { (void)lock; assert(!tree_held); tree_held = 1; return 0; }
static void vfs_lock_release(uintptr_t flags) { (void)flags; assert(tree_held); tree_held = 0; }
static int exists, fail_create, fail_allocate, fail_install;
static int xscore_vfs_resolve_path_at(uint32_t cwd, const char *path, uint32_t *node) {
    (void)cwd; assert(strcmp(path, "/dev/pts/0") == 0);
    *node = 1; return exists ? 0 : -AKAOS_ENOENT;
}
static int xscore_vfs_add_character_device(const char *path, uint32_t mode) {
    (void)path; (void)mode; assert(tree_held);
    if (fail_create) return 0;
    exists = 1; nodes[1].type = XSCORE_VNODE_CHARACTER; return 1;
}
static void xscore_vfs_node_release(uint32_t node) { assert(nodes[node].references); --nodes[node].references; }
struct buffer { int read_pos, write_pos, count; };
struct xscore_pty { struct buffer to_master, to_slave; uint32_t number, slave_node, master_descriptions, slave_descriptions; int in_use; };
static struct xscore_pty pty_pool[XSCORE_PTY_MAX];
static void pty_defaults(struct xscore_pty *p) { (void)p; }
struct xscore_file { uint32_t flags, references, node_id; size_t offset, size; void *data, *pipe; struct xscore_pty *pty; };
struct xscore_fd_table { int unused; };
static struct xscore_file file;
static uintptr_t file_pool_acquire(void) { assert(!pool_held); pool_held = 1; return 0; }
static void file_pool_release(uintptr_t flags) { (void)flags; assert(pool_held); pool_held = 0; }
static struct xscore_file *file_allocate(void) { assert(pool_held); return fail_allocate ? NULL : &file; }
static int fd_install(struct xscore_fd_table *t, struct xscore_file *f) { (void)t; (void)f; assert(pool_held); return fail_install ? -AKAOS_EMFILE : 3; }
'''
for name in ['current_credentials', 'node_allows', 'xscore_vfs_prepare_pts',
             'xscore_vfs_pts_access']:
    harness += function(vfs, name)
for name in ['xscore_fd_open_ptmx', 'xscore_fd_open_pts']:
    harness += function(files, name)
harness += r'''
int main(void) {
    struct xscore_fd_table table;
    assert(xscore_fd_open_ptmx(&table) == 3);
    assert(nodes[1].uid == 1000 && nodes[1].gid == 100);
    assert((nodes[1].mode & 0777) == 0620 && nodes[1].references == 1);
    assert(xscore_fd_open_pts(&table, 0, 1, 1, 1) == 3);
    uint32_t slaves = pty_pool[0].slave_descriptions;
    uid = 2000; gid = 200;
    for (int mode = 1; mode <= 3; ++mode)
        assert(xscore_fd_open_pts(&table, 0, 1, mode & 1, mode & 2) == -AKAOS_EACCES);
    assert(pty_pool[0].slave_descriptions == slaves);
    gid = 100;
    assert(xscore_fd_open_pts(&table, 0, 1, 0, 1) == 3);
    assert(xscore_fd_open_pts(&table, 0, 1, 1, 0) == -AKAOS_EACCES);
    uid = 1000; nodes[1].mode = AKAOS_MODE_CHARACTER | 0400;
    assert(xscore_fd_open_pts(&table, 0, 1, 1, 0) == 3);
    assert(xscore_fd_open_pts(&table, 0, 1, 0, 1) == -AKAOS_EACCES);
    uid = 0;
    assert(xscore_fd_open_pts(&table, 0, 1, 1, 1) == 3);
    assert(xscore_fd_open_pts(&table, 0, 2, 1, 1) == -AKAOS_ENODEV);
    nodes[1].unlinked = 1;
    assert(xscore_fd_open_pts(&table, 0, 1, 1, 1) == -AKAOS_ENODEV);
    nodes[1].unlinked = 0;
    /* Model the last close, then reuse the number for another effective uid. */
    xscore_vfs_node_release(1); pty_pool[0].in_use = 0;
    uid = 2000; gid = 200; nodes[1].mode |= 0777;
    assert(xscore_fd_open_ptmx(&table) == 3);
    assert(nodes[1].uid == 2000 && nodes[1].gid == 200);
    assert((nodes[1].mode & 0777) == 0620);
    uid = 1000; gid = 100;
    assert(xscore_fd_open_pts(&table, 0, 1, 1, 1) == -AKAOS_EACCES);
    uid = 2000; fail_install = 1;
    slaves = pty_pool[0].slave_descriptions;
    assert(xscore_fd_open_pts(&table, 0, 1, 1, 1) == -AKAOS_EMFILE);
    assert(pty_pool[0].slave_descriptions == slaves);
    xscore_vfs_node_release(1); pty_pool[0].in_use = 0;
    assert(xscore_fd_open_ptmx(&table) == -AKAOS_EMFILE);
    assert(!pty_pool[0].in_use && nodes[1].references == 0);
    fail_install = 0; fail_allocate = 1;
    assert(xscore_fd_open_ptmx(&table) == -AKAOS_ENFILE);
    assert(!pty_pool[0].in_use && nodes[1].references == 0);
    fail_allocate = 0; exists = 0; fail_create = 1;
    assert(xscore_fd_open_ptmx(&table) < 0);
    assert(!pty_pool[0].in_use && !pool_held && !tree_held);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-pty-') as tmp:
    source = Path(tmp) / 'test.c'
    binary = Path(tmp) / 'test'
    source.write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-I', str(ROOT / 'sdk/include'),
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PTY authorization regression passed')
