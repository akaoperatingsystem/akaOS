#!/usr/bin/env python3
# Copyright (c) 2026 akaOS Authors
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression for shared display queue bounds and ring behavior."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
QUEUES = (
    ('compositor', 'request', 'akaos_display_message', 'AKAOS_COMPOSITOR_MAX_MESSAGES'),
    ('wayland_input', 'wayland_input', 'akaos_wayland_input_event', 'AKAOS_WAYLAND_INPUT_CAPACITY'),
    ('shell_event', 'shell_event', 'akaos_shell_event', 'AKAOS_SHELL_EVENT_CAPACITY'),
    ('notification', 'notification', 'akaos_notification', 'AKAOS_NOTIFICATION_CAPACITY'),
)
SOURCE = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <akaos/display.h>
'''
TEST = r'''
static void test_FUNCTION(void) {
    struct akaos_compositor_channel channel = {0}, before;
    struct TYPE input, output, unchanged;
    memset(&input, 0x5a, sizeof(input));
    memset(&output, 0xa5, sizeof(output));
    unchanged = output;
    channel.abi_version = AKAOS_DISPLAY_ABI_VERSION;
    channel.protocol_version = AKAOS_COMPOSITOR_PROTOCOL_VERSION;
    const uint32_t bad[] = {CAPACITY, CAPACITY + 1U, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        for (unsigned which = 0; which < 3; ++which) {
            channel.PREFIX_head = which != 1 ? bad[i] : 0;
            channel.PREFIX_tail = which != 0 ? bad[i] : 0;
            before = channel;
            assert(!akaos_FUNCTION_submit(&channel, &input));
            assert(memcmp(&channel, &before, sizeof(channel)) == 0);
            assert(!akaos_FUNCTION_receive(&channel, &output));
            assert(memcmp(&channel, &before, sizeof(channel)) == 0);
            assert(memcmp(&output, &unchanged, sizeof(output)) == 0);
        }
    }
    /* Exercise every slot, FIFO ordering, full/empty, and repeated wraparound. */
    channel.PREFIX_head = channel.PREFIX_tail = CAPACITY - 1U;
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        assert(!akaos_FUNCTION_receive(&channel, &output));
        for (uint32_t i = 0; i < CAPACITY - 1U; ++i) {
            memset(&input, (int)i, sizeof(input));
            assert(akaos_FUNCTION_submit(&channel, &input));
        }
        before = channel;
        assert(!akaos_FUNCTION_submit(&channel, &input));
        assert(memcmp(&channel, &before, sizeof(channel)) == 0);
        for (uint32_t i = 0; i < CAPACITY - 1U; ++i) {
            memset(&input, (int)i, sizeof(input));
            assert(akaos_FUNCTION_receive(&channel, &output));
            assert(memcmp(&input, &output, sizeof(input)) == 0);
        }
        assert(channel.PREFIX_lock == 0);
        assert(!akaos_FUNCTION_receive(&channel, &output));
    }
}
'''
for function, prefix, event_type, capacity in QUEUES:
    test = TEST
    for key, value in (('FUNCTION', function), ('PREFIX', prefix),
                       ('TYPE', event_type), ('CAPACITY', capacity)):
        test = test.replace(key, value)
    SOURCE += test
SOURCE += '\nint main(void) {\n'
SOURCE += ''.join(f'    test_{queue[0]}();\n' for queue in QUEUES)
SOURCE += '    return 0;\n}\n'

with tempfile.TemporaryDirectory(prefix='akaos-display-queues-') as directory:
    source = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    source.write_text(SOURCE)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c17', '-O2', '-Wall',
                    '-Wextra', '-Werror', '-fsanitize=undefined,bounds',
                    '-fno-sanitize-recover=all', '-I' + str(ROOT / 'sdk/include'),
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('[PASS] All display queues reject invalid indices and preserve ring behavior')
