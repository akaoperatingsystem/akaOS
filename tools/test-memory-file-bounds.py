#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise the production memory-read branch with host address/UB sanitizers."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'xscore/kernel/file.c').read_text()
start = source.index('    if (file->flags & FILE_MEMORY) {',
                     source.index('static int64_t fd_read_file('))
end = source.index('    if (file->flags & FILE_SHM)', start)
# Only the memory branch is extracted; other device handlers need the kernel.
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define FILE_MEMORY 8
struct xscore_file {
    uint32_t flags;
    uint64_t size, offset;
    const uint8_t *data;
};
static int64_t read_memory(struct xscore_file *file, void *buffer, uint64_t length) {
''' + source[start:end] + r'''
    abort();
}
int main(void) {
    uint8_t *data = malloc(4);
    assert(data);
    memcpy(data, "test", 4);
    struct xscore_file file = {FILE_MEMORY, 4, 0, data};
    uint8_t out[8];
    memset(out, 0xa5, sizeof(out));
    assert(read_memory(&file, out, 2) == 2);
    assert(file.offset == 2 && memcmp(out, "te", 2) == 0);
    assert(read_memory(&file, out, sizeof(out)) == 2);
    assert(file.offset == 4 && memcmp(out, "st", 2) == 0);
    assert(out[2] == 0xa5);
    const uint64_t offsets[] = {4, 5, INT64_MAX, UINT64_MAX};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        file.offset = offsets[i];
        memset(out, 0xa5, sizeof(out));
        assert(read_memory(&file, out, sizeof(out)) == 0);
        assert(file.offset == offsets[i]);
        for (unsigned j = 0; j < sizeof(out); ++j) assert(out[j] == 0xa5);
    }
    file.offset = 1;
    assert(read_memory(&file, out, 0) == 0 && file.offset == 1);
    file.offset = 0;
    assert(read_memory(&file, out, UINT64_MAX) == 4);
    assert(file.offset == 4 && memcmp(out, "test", 4) == 0);
    free(data);
    file.data = NULL;
    file.size = 0;
    file.offset = 0;
    assert(read_memory(&file, out, sizeof(out)) == 0);
    file.offset = 1;
    assert(read_memory(&file, out, sizeof(out)) == 0 && file.offset == 1);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-memory-bounds-') as directory:
    path = Path(directory)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Memory-file bounds regression passed')
