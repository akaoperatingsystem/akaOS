#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Host regressions using production cp and VFS metadata functions."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
# Reuse the VFS host fixture definitions, without executing its tests.
fixture = (ROOT / 'tools/test-vfs-open-access.py').read_text()
namespace = {'__file__': str(ROOT / 'tools/test-vfs-open-access.py')}
exec(fixture[:fixture.index('# Compile the complete production opener')], namespace)
function = namespace['function']
harness = namespace['harness']
for name in ['current_credentials', 'name_equals', 'find_child', 'node_allows',
             'split_path', 'xscore_vfs_resolve_path_at', 'xscore_vfs_initialize',
             'vfs_chmod', 'vfs_chown', 'xscore_vfs_chmod_node', 'xscore_vfs_chown_node']:
    harness += function(name)
harness += r'''
int main(void) {
    assert(xscore_vfs_initialize());
    vfs_locked = 1;
    node_count = 3;
    nodes[1].type = nodes[2].type = XSCORE_VNODE_REGULAR;
    nodes[1].is_dynamic = nodes[2].is_dynamic = 1;
    nodes[1].uid = 1000; nodes[1].gid = 1000;
    nodes[1].mode = AKAOS_MODE_REGULAR | 0600;
    nodes[2].mode = AKAOS_MODE_REGULAR | 0600;
    /* Destination was replaced; the open inode remains live after unlink. */
    nodes[1].unlinked = 1; nodes[1].references = 1;
    strcpy(nodes[2].name, "destination");
    struct xscore_vnode replacement = nodes[2];
    uid = 2000;
    assert(xscore_vfs_chmod_node(1, 04755) == -AKAOS_EPERM);
    assert(xscore_vfs_chown_node(1, 0, 0) == -AKAOS_EPERM);
    assert(nodes[1].uid == 1000 && nodes[1].mode == (AKAOS_MODE_REGULAR | 0600));
    uid = 1000;
    assert(xscore_vfs_chmod_node(1, 04755) == 0);
    uid = 0;
    assert(xscore_vfs_chown_node(1, 0, 0) == 0);
    assert(nodes[1].uid == 0 && !(nodes[1].mode & 06000));
    assert(xscore_vfs_chmod_node(1, 04755) == 0);
    assert(nodes[1].mode == (AKAOS_MODE_REGULAR | 04755));
    assert(memcmp(&replacement, &nodes[2], sizeof(replacement)) == 0);
    assert(xscore_vfs_chown_node(1, UINT32_MAX, UINT32_MAX) == 0);
    assert(nodes[1].uid == 0 && nodes[1].gid == 0);
    assert(xscore_vfs_chmod_node(63, 0777) == -AKAOS_ENOENT);
    assert(!held);
}
'''
# Exercise the descriptor bridge with a reference held across VFS mutation.
namespace['source'] = (ROOT / 'xscore/kernel/file.c').read_text()
file_harness = r'''
#include <assert.h>
#include <stdint.h>
#include <akaos/errno.h>
#define FILE_VNODE 32
#define FILE_DIRECTORY 64
struct xscore_fd_table { int unused; };
struct xscore_file { unsigned flags; uint32_t node_id; };
static struct xscore_file file;
static int references, locked, ownership_seen;
static struct xscore_file *fd_lookup_ref(struct xscore_fd_table *t, int fd) {
    (void)t; if (fd != 3) return 0; ++references; return &file;
}
static uintptr_t file_operation_acquire(struct xscore_file *f) {
    assert(f == &file && references == 1 && !locked); locked = 1; return 0;
}
static void file_operation_release(struct xscore_file *f, uintptr_t flags) {
    (void)f; (void)flags; assert(locked); locked = 0;
}
static void file_put(struct xscore_file *f) {
    (void)f; assert(!locked && references == 1); --references;
}
static int xscore_vfs_chown_node(uint32_t node, uint32_t uid, uint32_t gid) {
    assert(references == 1 && locked && node == 12 && uid == 23 && gid == 24);
    ownership_seen++; return -AKAOS_EPERM;
}
static int xscore_vfs_chmod_node(uint32_t node, uint32_t mode) {
    assert(references == 1 && locked && node == 12 && mode == 04755);
    return 0;
}
''' + function('xscore_fd_metadata') + r'''
int main(void) {
    struct xscore_fd_table table;
    assert(xscore_fd_metadata(&table, -1, 0, 0, 0) == -AKAOS_EBADF);
    assert(xscore_fd_metadata(&table, 3, 0, 0, 0) == -AKAOS_EBADF);
    file.node_id = 12;
    for (int directory = 0; directory < 2; ++directory) {
        file.flags = directory ? FILE_DIRECTORY : FILE_VNODE;
        assert(xscore_fd_metadata(&table, 3, 0, 04755, 0) == 0);
        assert(xscore_fd_metadata(&table, 3, 1, 23, 24) == -AKAOS_EPERM);
        assert(!references && !locked);
    }
    assert(ownership_seen == 2);
}
'''
cp_harness = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <akaos/fs.h>
static const char *victim;
static char saved[512];
static int swap_done, fail_metadata;
static ssize_t intercepted_write(int fd, const void *data, size_t size) {
    ssize_t result = write(fd, data, size);
    if (!swap_done) {
        swap_done = 1;
        assert(rename(victim, saved) == 0);
        int replacement = open(victim, O_WRONLY | O_CREAT | O_EXCL, 0600);
        assert(replacement >= 0);
        assert(write(replacement, "attacker", 8) == 8);
        close(replacement);
    }
    return result;
}
static int intercepted_fchmod(int fd, mode_t mode) {
    if (fail_metadata) { errno = EPERM; return -1; }
    return fchmod(fd, mode);
}
static ssize_t getdents(int fd, struct akaos_dirent *entries, size_t count) {
    (void)fd; (void)entries; (void)count; return 0;
}
#define write intercepted_write
#define fchmod intercepted_fchmod
#define main cp_main
#include "userspace/bin/cp/main.c"
#undef main
#undef write
#undef fchmod
int main(int argc, char **argv) {
    assert(argc == 2);
    char src[512], dst[512];
    snprintf(src, sizeof(src), "%s/src", argv[1]);
    snprintf(dst, sizeof(dst), "%s/dst", argv[1]);
    snprintf(saved, sizeof(saved), "%s/copied", argv[1]);
    int fd = open(src, O_WRONLY | O_CREAT, 0600);
    assert(fd >= 0 && write(fd, "trusted", 7) == 7); close(fd);
    assert(chmod(src, 04755) == 0);
    victim = dst; opt_preserve = 1;
    assert(copy_file(src, dst) == 0);
    struct stat st;
    assert(stat(dst, &st) == 0 && (st.st_mode & 07777) == 0600);
    assert(stat(saved, &st) == 0 && (st.st_mode & 07777) == 04755);
    char contents[8]; fd = open(dst, O_RDONLY);
    assert(read(fd, contents, 8) == 8 && !memcmp(contents, "attacker", 8)); close(fd);
    fail_metadata = 1;
    assert(copy_file(src, dst) == 1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    for name, code in [('vfs', harness), ('file', file_harness), ('cp', cp_harness)]:
        source = temp / (name + '.c')
        binary = temp / name
        source.write_text(code)
        subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-I' + str(ROOT),
                        '-I' + str(ROOT / 'sdk/include'), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary), directory], check=True)
print('cp pathname replacement, metadata failure, and VFS authorization regressions passed')
