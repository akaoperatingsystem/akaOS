#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression for open access-mode authorization using production VFS paths."""
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
                  source.index('static void vfs_append_str')]
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
/* Storage allocation and persistence are outside this host regression. */
static int freed_blocks;
static void free_vnode_blocks(struct xscore_vnode *node) {
    ++freed_blocks;
    memset(node->block_pages, 0, sizeof(node->block_pages));
}
static int convert_to_dynamic(struct xscore_vnode *node) { (void)node; assert(0); return 0; }
static int xscore_vfs_persistence_sync(void) { assert(0); return 0; }
'''
# Compile the complete production opener; procfs is disabled in these tests.
open_body = function('xscore_vfs_open_at')
known = {'xscore_vfs_open_at', 'xscore_vfs_resolve_path_at',
         'xscore_spin_lock_irqsave', 'xscore_vfs_persistence_sync',
         'xscore_fd_open_vnode'}
harness += '#define vfs_append_u64(...) assert(0)\n#define vfs_append_str(...) assert(0)\n'
for name in sorted(set(re.findall(r'\b(xscore_\w+)\(', open_body)) - known):
    harness += f'#define {name}(...) (assert(0), 0)\n'
harness += r'''
#define FILE_READABLE 1
#define FILE_WRITABLE 2
#define FILE_VNODE 32
#define FILE_APPEND 256
#define XSCORE_FILE_POOL_SIZE 1
struct xscore_fd_table { int unused; };
struct xscore_file {
    uint32_t flags, references, node_id, flock_type;
    uint64_t offset, size;
    const uint8_t *data;
};
static struct xscore_file file_pool[XSCORE_FILE_POOL_SIZE];
static int acquired;
static int xscore_vfs_node_acquire(uint32_t node) {
    assert(node == 1); ++acquired; return 1;
}
static void xscore_vfs_node_release(uint32_t node) { assert(node == 1); --acquired; }
static uintptr_t file_pool_acquire(void) { assert(!held); return 0; }
static void file_pool_release(uintptr_t flags) { (void)flags; }
static int fd_install(struct xscore_fd_table *table, struct xscore_file *file) {
    assert(table && file == file_pool); return 0;
}
'''
file_source = (ROOT / 'xscore/kernel/file.c').read_text()
original_source = source
source = file_source
harness += function('xscore_fd_open_vnode')
source = original_source
for name in ['current_credentials', 'node_allows', 'name_equals', 'find_child',
             'split_path', 'xscore_vfs_resolve_path_at', 'xscore_vfs_initialize']:
    harness += function(name)
harness += open_body
harness += r'''
int main(void) {
    struct xscore_fd_table table = {0};
    assert(xscore_vfs_initialize());
    nodes[1].type = XSCORE_VNODE_REGULAR;
    nodes[1].parent = 0;
    strcpy(nodes[1].name, "secret");
    nodes[1].uid = 2000; nodes[1].gid = 2000;
    nodes[1].is_dynamic = 1;
    nodes[1].size = 42;
    node_count = 2;
    uid = gid = 1000;
    for (unsigned mode = 0; mode <= 0777; ++mode) {
        nodes[1].mode = AKAOS_MODE_REGULAR | mode;
        for (uint64_t access = 0; access < 3; ++access) {
            memset(file_pool, 0, sizeof(file_pool)); acquired = 0;
            int allowed = access == AKAOS_O_RDONLY ? (mode & 4) != 0 :
                          access == AKAOS_O_WRONLY ? (mode & 2) != 0 : (mode & 6) == 6;
            assert(xscore_vfs_open_at(&table, 0, "/secret", access | AKAOS_O_APPEND, 0)
                   == (allowed ? 0 : -AKAOS_EACCES));
            if (allowed) {
                assert(acquired == 1);
                assert(!!(file_pool[0].flags & FILE_READABLE) == (access != AKAOS_O_WRONLY));
                assert(!!(file_pool[0].flags & FILE_WRITABLE) == (access != AKAOS_O_RDONLY));
                assert(file_pool[0].flags & FILE_APPEND);
            } else assert(acquired == 0);
            assert(!held && nodes[1].size == 42);
        }
    }
    /* Truncation must authorize writes before changing either backing type. */
    static const uint8_t contents[] = "private contents";
    for (unsigned user = 0; user < 4; ++user) {
        uid = user == 0 ? 0 : (user == 1 ? 2000 : 1000);
        gid = user == 2 ? 2000 : uid;
        for (unsigned dynamic = 0; dynamic < 2; ++dynamic) {
            for (unsigned mode = 0; mode <= 0777; ++mode) {
                for (uint64_t access = 0; access < 3; ++access) {
                    nodes[1].mode = AKAOS_MODE_REGULAR | mode;
                    nodes[1].is_dynamic = dynamic;
                    nodes[1].data = dynamic ? NULL : contents;
                    nodes[1].size = sizeof(contents);
                    nodes[1].block_pages[0] = dynamic ? 123 : 0;
                    struct xscore_vnode before = nodes[1];
                    memset(file_pool, 0, sizeof(file_pool)); acquired = 0;
                    freed_blocks = 0;
                    unsigned shift = user == 1 ? 6 : (user == 2 ? 3 : 0);
                    unsigned requested = access == AKAOS_O_RDWR ? 6 : 2;
                    int allowed = user == 0 || ((mode >> shift) & requested) == requested;
                    int expected = access == AKAOS_O_RDONLY ? -AKAOS_EINVAL :
                        !allowed ? -AKAOS_EACCES :
                        (!dynamic && !(mode & AKAOS_MODE_WRITE)) ? -AKAOS_EROFS : 0;
                    assert(xscore_vfs_open_at(&table, 0, "/secret",
                                             access | AKAOS_O_TRUNC, 0) == expected);
                    assert(!held);
                    if (expected) {
                        assert(memcmp(&before, &nodes[1], sizeof(before)) == 0);
                        assert(!freed_blocks && !acquired && !file_pool[0].flags);
                    } else {
                        assert(nodes[1].is_dynamic && !nodes[1].size);
                        assert(!nodes[1].block_pages[0] && freed_blocks == 1);
                        assert(acquired == 1 && (file_pool[0].flags & FILE_WRITABLE));
                        if (!dynamic) assert(!nodes[1].data);
                    }
                }
            }
        }
    }
    /* Invalid truncation must not create a file, even for root. */
    for (unsigned user = 0; user < 2; ++user) {
        uid = gid = user ? 1000 : 0;
        struct xscore_vnode before[XSCORE_VFS_MAX_NODES];
        memcpy(before, nodes, sizeof(nodes));
        assert(xscore_vfs_open_at(&table, 0, "/missing",
                                 AKAOS_O_RDONLY | AKAOS_O_CREAT | AKAOS_O_TRUNC,
                                 0600) == -AKAOS_EINVAL);
        assert(memcmp(before, nodes, sizeof(nodes)) == 0 && node_count == 2 && !held);
    }
    nodes[1].mode = AKAOS_MODE_REGULAR; /* No read or write permission. */
    const uint64_t extras[] = {0, AKAOS_O_CREAT, AKAOS_O_TRUNC,
        AKAOS_O_CREAT | AKAOS_O_EXCL, AKAOS_O_APPEND,
        AKAOS_O_CREAT | AKAOS_O_TRUNC};
    const char *paths[] = {"/secret", "/missing", "/"};
    for (unsigned user = 0; user < 2; ++user) {
        uid = gid = user ? 1000 : 0;
        for (unsigned p = 0; p < 3; ++p) {
            for (unsigned e = 0; e < sizeof(extras)/sizeof(extras[0]); ++e) {
                struct xscore_vnode before[XSCORE_VFS_MAX_NODES];
                memcpy(before, nodes, sizeof(nodes));
                memset(file_pool, 0, sizeof(file_pool)); acquired = 0;
                assert(xscore_vfs_open_at(&table, 0, paths[p], 3 | extras[e], 0600)
                       == -AKAOS_EINVAL);
                assert(memcmp(before, nodes, sizeof(nodes)) == 0);
                assert(node_count == 2 && !held && acquired == 0);
                assert(xscore_fd_open_vnode(&table, 1, 3 | extras[e]) == -AKAOS_EINVAL);
                assert(acquired == 0 && file_pool[0].flags == 0);
            }
        }
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-open-access-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-variable', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-I', str(ROOT / 'sdk/include'),
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('VFS open access-mode regression passed')
