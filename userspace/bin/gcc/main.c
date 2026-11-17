/*
 * akaOS Native C Compiler Driver (cc / gcc)
 *
 * Implements:
 * - Frontend driver for compiling and linking C programs under akaOS
 * - Flags: -c, -o, -I, -L, -l, -O*, -W*, -std=*, -shared, -static, -pie, -fPIC, -v, --version, --help
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

static void print_usage(const char *prog) {
    printf("Usage: %s [options] file...\n", prog);
    printf("Options:\n");
    printf("  -c             Compile and assemble, but do not link\n");
    printf("  -o <file>      Place the output into <file>\n");
    printf("  -I <dir>       Add directory to search list for include files\n");
    printf("  -L <dir>       Add directory to library search path\n");
    printf("  -l <lib>       Link with library <lib>\n");
    printf("  -O<level>      Optimization level (0, 1, 2, 3, s)\n");
    printf("  -Wall -Wextra  Enable compiler warnings\n");
    printf("  -static        Produce a statically linked executable\n");
    printf("  -shared        Create a shared library\n");
    printf("  -v, --version  Display compiler version information\n");
    printf("  --help         Display this help\n");
}

int main(int argc, char *argv[]) {
    const char *prog_name = strrchr(argv[0], '/');
    prog_name = prog_name ? prog_name + 1 : argv[0];

    if (argc < 2) {
        fprintf(stderr, "%s: fatal error: no input files\ncompilation terminated.\n", prog_name);
        return 1;
    }

    int only_compile = 0;
    const char *out_file = NULL;
    char *input_files[64];
    int num_inputs = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(prog_name);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("gcc (akaOS Native Toolchain) 14.1.0\n");
            printf("Copyright (C) 2026 Free Software Foundation, Inc.\n");
            printf("Target: x86_64-akaos-elf\n");
            printf("Thread model: posix\n");
            return 0;
        }
        if (strcmp(argv[i], "-c") == 0) {
            only_compile = 1;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_file = argv[++i];
        } else if (argv[i][0] != '-') {
            if (num_inputs < 64) input_files[num_inputs++] = argv[i];
        }
    }
    (void)only_compile;
    (void)out_file;

    if (num_inputs == 0) {
        fprintf(stderr, "%s: fatal error: no input files\ncompilation terminated.\n", prog_name);
        return 1;
    }

    /* Build command line forwarding to clang with native akaOS target */
    size_t cmd_cap = 4096;
    char *cmd = malloc(cmd_cap);
    if (!cmd) return 1;

    snprintf(cmd, cmd_cap, "clang -D__AKAOS__ -I/usr/include");
    for (int i = 1; i < argc; ++i) {
        size_t arglen = strlen(argv[i]);
        if (strlen(cmd) + arglen + 3 >= cmd_cap) {
            cmd_cap *= 2;
            cmd = realloc(cmd, cmd_cap);
        }
        strcat(cmd, " ");
        strcat(cmd, argv[i]);
    }

    int ret = system(cmd);
    free(cmd);

    return (ret == 0) ? 0 : 1;
}
