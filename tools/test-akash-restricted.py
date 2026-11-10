#!/usr/bin/env python3
# Copyright (c) 2026 akaOS Authors
# SPDX-License-Identifier: BSD-3-Clause
"""Host checks of akash's real parser, variable controls and execution paths.

Adapt akaOS account/stat interfaces to the host; directory glob enumeration is
stubbed. Actual fork/exec and pipelines run against synthetic executables.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'userspace/bin/akash/main.c').read_text()
source = '\n'.join(line for line in source.splitlines()
                   if not line.startswith('#include <akaos/'))
source = source.replace('struct akaos_stat', 'struct stat')
source = source.replace('st.mode', 'st.st_mode').replace('st.size', 'st.st_size')
source = source.replace('st.type == AKAOS_DT_DIRECTORY', 'S_ISDIR(st.st_mode)')
source = source.replace('int main(', 'static int akash_main(')
compat = r'''
#define _GNU_SOURCE
#include <sys/types.h>
#include <stddef.h>
#include <unistd.h>
#define AKAOS_ARCH_NAME "host-test"
#define AKAOS_ARCH_BITS 64
struct akaos_account {
    char name[32]; uid_t uid; gid_t gid; char home[128]; char shell[128];
};
static int akaos_account_by_uid(uid_t uid, struct akaos_account *a) {
    (void)uid; (void)a; return 0;
}
struct akaos_dirent { char name[256]; };
static ssize_t getdents(int fd, struct akaos_dirent *entries, size_t count) {
    (void)fd; (void)entries; (void)count; return 0;
}
'''
driver = r'''
int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--test-builtin") == 0) {
        opt_restricted_shell = 1;
        execute_builtin(argc - 2, argv + 2);
        return last_exit_status;
    }
    return akash_main(argc, argv);
}
'''
with tempfile.TemporaryDirectory(prefix='akash-restricted-') as temp:
    temp = Path(temp)
    c_file, binary = temp / 'test.c', temp / 'akash'
    c_file.write_text(compat + source + driver)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-w',
                    '-fsanitize=undefined', '-include',
                    str(ROOT / 'sdk/include/akaos/version.h'),
                    str(c_file), '-o', str(binary)], check=True)
    allowed = temp / 'allowed'
    outside = temp / 'outside'
    allowed.mkdir()
    outside.mkdir()
    # Use native binaries so the test does not depend on script fallback behavior.
    probe_source = temp / 'probe.c'
    probe_source.write_text('#include <stdio.h>\nint main(int n, char **v) { '
                            'for (int i = 1; i < n; ++i) '
                            'printf("%s%s", i > 1 ? " " : "", v[i]); '
                            'puts(""); return 0; }\n')
    for directory in (allowed, outside):
        subprocess.run([os.environ.get('CC', 'cc'), str(probe_source),
                        '-o', str(directory / 'probe')], check=True)
    env = dict(os.environ, PATH=str(allowed))

    def run(command, restricted=True):
        result = subprocess.run([str(binary), *(['-r'] if restricted else []),
                                 '-c', command], env=env, cwd=temp,
                                capture_output=True, text=True, timeout=5)
        return result

    for path in (str(outside / 'probe'), './outside/probe', 'outside/probe',
                 '../' + temp.name + '/outside/probe'):
        for command in (f'{path} ESCAPED', f'command {path} ESCAPED',
                        f'{path} ESCAPED | probe', f'probe x | {path} ESCAPED',
                        f'{path} ESCAPED &'):
            result = run(command)
            assert 'ESCAPED' not in result.stdout, (command, result)
            assert 'Permission denied' in result.stderr, (command, result)
        assert run(f'{path} permitted', False).stdout == 'permitted\n', (path, run(f'{path} permitted', False))
    for command in (f'X={outside}/probe; $X ESCAPED',
                    f'alias escape={outside}/probe; escape ESCAPED'):
        result = run(command)
        assert 'ESCAPED' not in result.stdout and 'Permission denied' in result.stderr
    for mode in ('-r', '--restricted'):
        result = subprocess.run([str(binary), mode, '-c', 'probe allowed'],
                                env=env, capture_output=True, text=True, check=True)
        assert result.stdout == 'allowed\n'
    rbash = temp / 'rbash'
    rbash.symlink_to(binary)
    result = subprocess.run([str(rbash), '-c', f'{outside}/probe ESCAPED'],
                            env=env, capture_output=True, text=True)
    assert result.returncode == 126 and 'ESCAPED' not in result.stdout
    for command in ('probe allowed', 'command probe allowed', 'probe allowed | probe allowed'):
        assert run(command).stdout == 'allowed\n', command
    for variable in ('PATH', 'SHELL', 'ENV', 'BASH_ENV'):
        for assignment in (f'{variable}={outside}', f'export {variable}={outside}',
                           f'declare -x {variable}={outside}',
                           f'typeset -x {variable}={outside}',
                           f'declare -n ref={variable}; declare -x ref={outside}'):
            result = run(assignment)
            assert result.returncode == 1 and 'restricted mode' in result.stderr, (assignment, result)
    result = run(f'declare -x PATH={outside}; probe protected')
    assert result.stdout == 'protected\n' and 'restricted mode' in result.stderr
    # Exercise the builtin dispatcher directly, including nested builtin calls.
    for args in (['command', str(outside / 'probe'), 'ESCAPED'],
                 ['builtin', 'command', str(outside / 'probe'), 'ESCAPED'],
                 ['command', '-p', 'echo', 'ESCAPED']):
        result = subprocess.run([str(binary), '--test-builtin', *args], env=env,
                                capture_output=True, text=True, timeout=5)
        assert result.returncode != 0 and 'ESCAPED' not in result.stdout, (args, result)
print('akash restricted execution regression checks passed')
