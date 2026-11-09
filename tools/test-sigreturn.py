#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression tests for the actual SIGRETURN C paths with mocked mappings."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'xscore/kernel/process.c').read_text()
frames = source[source.index('#define XSCORE_SIGFRAME_MAGIC'):source.index('int64_t xscore_process_sigaction')]
restore = source[source.index('#if defined(__x86_64__) || defined(__i386__) || defined(__arm__)\n/* Validate'):source.index('int xscore_process_signal_pending_current')]
PRELUDE = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#undef __x86_64__
#define AKAOS_EINVAL 22
#define XSCORE_PAGE_USER 4
#define XSCORE_PAGE_NO_EXECUTE (UINT64_C(1) << 63)
struct akaos_siginfo { char unused[32]; };
struct xscore_process {
    void *fork_context, *last_syscall_frame;
    uint32_t signal_mask;
    int fork_resume;
};
struct xscore_user_transition { uint32_t user_rsp; };
static struct xscore_process process;
static struct xscore_user_transition transition;
static uint64_t mapping_flags = XSCORE_PAGE_USER;
static int readable = 1, writable = 1, copy_ok = 1;
static struct xscore_process *xscore_process_current(void) { return &process; }
static struct xscore_user_transition *xscore_thread_current_user_transition(void)
    __attribute__((unused));
static struct xscore_user_transition *xscore_thread_current_user_transition(void) { return &transition; }
static int xscore_user_range_prepare(uint64_t a, size_t n, int w) {
    assert(n == 1);
    return a >= 0x1000 && a < 0x3000 && (w ? writable : readable)
           && (mapping_flags & XSCORE_PAGE_USER);
}
static int xscore_vmm_is_mapped(uint64_t a, uint64_t *f) {
    *f = mapping_flags;
    return a >= 0x1000 && a < 0x3000;
}
'''
TEST = r'''
static struct xscore_sigframe input;
static int xscore_copy_from_user(void *dst, uint64_t a, size_t n) {
    (void)a;
    if (!copy_ok) return 0;
    memcpy(dst, &input, n);
    return 1;
}
'''
MAIN = r'''
int main(void) {
    WORD live[32] = {0}, saved[32] = {0}, before[32];
    process.last_syscall_frame = live;
    process.fork_context = saved;
    input.magic = XSCORE_SIGFRAME_MAGIC;
    input.PC = 0x1000;
    input.SP = 0x3000; /* one past the writable mapping */
    input.STATUS = ~(WORD)0;
    input.signal_mask = 0x40;
    assert(xscore_process_sigreturn() == 0);
    ASSERT_STATUS
    assert(process.fork_resume == 1 && process.signal_mask == 0x40);
    assert(memcmp(live, saved, SLOTS * sizeof(WORD)) == 0);
    for (int bad = 0; bad < 7; ++bad) {
        input.PC = 0x1000;
        input.SP = 0x3000;
        input.magic = XSCORE_SIGFRAME_MAGIC;
        mapping_flags = XSCORE_PAGE_USER;
        readable = writable = copy_ok = 1;
        if (bad == 0) input.PC = 0;
        if (bad == 1) input.SP = 0;
        if (bad == 2) mapping_flags |= XSCORE_PAGE_NO_EXECUTE;
        if (bad == 3) mapping_flags = 0;
        if (bad == 4) writable = 0;
        if (bad == 5) input.magic = 0;
        if (bad == 6) copy_ok = 0;
        memcpy(before, live, sizeof live);
        process.fork_resume = 0;
        process.signal_mask = 0x80;
        assert(xscore_process_sigreturn() == -AKAOS_EINVAL);
        assert(memcmp(before, live, sizeof live) == 0);
        assert(memcmp(before, saved, SLOTS * sizeof(WORD)) == 0);
        assert(process.fork_resume == 0 && process.signal_mask == 0x80);
    }
    EXTRA
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-sigreturn-') as tmp:
    for arch, word, pc, sp, status, slots, checks, extra in [
        ('__x86_64__', 'uint64_t', 'rip', 'rsp', 'rflags', 20,
         'assert(live[16] == 0x23 && live[19] == 0x1b); assert(live[17] == 0x250fd7);',
         'assert(!process_sigreturn_addresses_valid(UINT64_C(0x1000000001000), 0x3000));'),
        ('__i386__', 'uint32_t', 'eip', 'user_esp', 'eflags', 11,
         'assert(live[7] == 0x1b && live[10] == 0x23); assert(live[8] == 0x250fd7);', ''),
        ('__arm__', 'uint32_t', 'pc', 'sp', 'cpsr', 15,
         'assert(live[0] == 0xfe0ffc30);',
         'copy_ok = writable = readable = 1; mapping_flags = XSCORE_PAGE_USER; '
         'input.magic = XSCORE_SIGFRAME_MAGIC; input.sp = 0x3000; '
         'input.pc = 0x1002; input.cpsr = 0x10; '
         'assert(xscore_process_sigreturn() == -AKAOS_EINVAL); '
         'input.cpsr = 0x30; assert(xscore_process_sigreturn() == 0);'),
    ]:
        main = MAIN
        for key, value in sorted(dict(WORD=word, PC=pc, SP=sp, STATUS=status,
                               SLOTS=str(slots), ASSERT_STATUS=checks, EXTRA=extra).items(), key=lambda item: -len(item[0])):
            main = main.replace(key, value)
        path = Path(tmp) / (arch + '.c')
        path.write_text(PRELUDE + '\n#define ' + arch + ' 1\n' + frames + TEST + restore + main)
        exe = path.with_suffix('')
        subprocess.run(['clang', '-std=c17', '-Wall', '-Wextra', '-Werror', str(path), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        print(f'[PASS] {arch}: privilege sanitization, address validation, atomic rejection')
