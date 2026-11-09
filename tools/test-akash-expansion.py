#!/usr/bin/env python3
# Copyright (c) 2026 akaOS Authors
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression checks of the actual akash parsing/expansion functions.

Only execution, aliases and filesystem globbing are stubbed: capture the argument
structures at the dispatch boundary without needing to boot a guest.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'userspace/bin/akash/main.c').read_text()


def section(start, end):
    return source[source.index(start):source.index(end, source.index(start))]


harness = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define MAX_ARGS 64
#define MAX_STAGES 8
#define GLOB_PATH_MAX 128
#define AKAOS_ARCH_NAME "test"
static int last_exit_status, script_argc;
static char script_arg0[128] = "akash", script_args[10][128];
static struct { char name[128], home[128]; } shell_account;
static const char *get_shell_var(const char *name) { return getenv(name); }
static int is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}
'''
harness += section('static void expand_word(', 'static int has_wildcard(')
harness += r'''
static void expand_stage_globs(struct command_stage *stage) { (void)stage; }
static void dump(const char *s) {
    if (!s) { puts("-"); return; }
    while (*s) printf("%02x", (unsigned char)*s++);
    puts("");
}
static int execute_pipeline(struct command_stage *stages, int count) {
    printf("pipeline %d\n", count);
    for (int i = 0; i < count; ++i) {
        printf("stage %d %d\n", stages[i].background, stages[i].argc);
        for (int j = 0; j < stages[i].argc; ++j) dump(stages[i].argv[j]);
        dump(stages[i].input_file);
        dump(stages[i].output_file);
    }
    return 1;
}
static void expand_aliases(const char *src, char *dst, size_t size) {
    snprintf(dst, size, "%s", src);
}
'''
harness += section('static int execute_pipeline_string(', '/* Statement tokenizer for control flow */')
harness += r'''
int main(int argc, char **argv) {
    for (int i = 2; i < argc && script_argc < 10; ++i)
        snprintf(script_args[script_argc++], 128, "%s", argv[i]);
    execute_command_line(argv[1]);
    return 0;
}
'''


def stage(args, background=0, input_file=None, output_file=None):
    values = args + [input_file, output_file]
    return [f'stage {background} {len(args)}'] + [
        '-' if value is None else value.encode().hex() for value in values]


with tempfile.TemporaryDirectory(prefix='akash-expansion-') as temp:
    c_file = Path(temp) / 'test.c'
    binary = Path(temp) / 'test'
    c_file.write_text(harness)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-Wno-unused-function', '-fsanitize=undefined',
                    str(c_file), '-o', str(binary)], check=True)

    def check(command, args, expected, env=None):
        result = subprocess.run([str(binary), command, *args], check=True,
                                capture_output=True, text=True, env=env)
        assert result.stdout.splitlines() == expected, (command, args, result.stdout, expected)

    payloads = ['x; echo injected', 'x && echo injected', 'x || echo injected',
                'x | echo injected', 'x > victim', 'x < victim', '&',
                '\"; echo injected; echo \"', "'; echo injected; echo '",
                'x\necho injected', '$1 `echo injected`', 'back\\slash', '']
    for payload in payloads:
        check('echo "$1"', [payload], ['pipeline 1'] + stage(['echo', payload]))
        fields = payload.split()  # fixtures use shell whitespace only
        check('echo $1', [payload], ['pipeline 1'] + stage(['echo'] + fields))
        check('echo prefix"$1"suffix', [payload],
              ['pipeline 1'] + stage(['echo', 'prefix' + payload + 'suffix']))
    check('echo "$@"', ['a;', 'b|'], ['pipeline 1'] + stage(['echo', 'a; b|']))
    check('echo "$0" "$#" "$?"', ['a'],
          ['pipeline 1'] + stage(['echo', 'akash', '1', '0']))
    check("echo '$1;|&'", ['unused'], ['pipeline 1'] + stage(['echo', '$1;|&']))
    check('echo "literal|pipe" | cat', [],
          ['pipeline 2'] + stage(['echo', 'literal|pipe']) + stage(['cat']))
    check('echo "$1" > "$2"', ['safe', 'file > literal'],
          ['pipeline 1'] + stage(['echo', 'safe'], output_file='file > literal'))
    check('echo "$1" & echo next', ['&'],
          ['pipeline 1'] + stage(['echo', '&'], background=1) +
          ['pipeline 1'] + stage(['echo', 'next']))
    check('echo $1; echo next', [';'],
          ['pipeline 1'] + stage(['echo', ';']) + ['pipeline 1'] + stage(['echo', 'next']))
    check('echo "$VALUE"', [], ['pipeline 1'] + stage(['echo', '\"; echo injected']),
          dict(os.environ, VALUE='\"; echo injected'))
    check('echo "$1"', ['x' * 127], ['pipeline 1'] + stage(['echo', 'x' * 127]))
print('akash expansion regression checks passed')
