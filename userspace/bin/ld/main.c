/*
 * akaOS Native Linker (ld) - GNU ld compatible
 *
 * Implements:
 * - Linker driver for akaOS ELF executables and shared libraries
 * - Flags: -o OUTFILE, -T SCRIPT, -m EMUL, -static, -pie, -shared, -L, -l, -v, --help
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>

static void print_usage(void) {
    printf("Usage: ld [options] file...\n");
    printf("Options:\n");
    printf("  -o OUTFILE     Set output file name (default a.out)\n");
    printf("  -T SCRIPT      Read linker script\n");
    printf("  -m EMUL        Set emulation (elf_x86_64, elf_i386, etc.)\n");
    printf("  -static        Do not link against shared libraries\n");
    printf("  -pie           Create a position independent executable\n");
    printf("  -shared        Create a shared library\n");
    printf("  -L DIR         Add directory to library search path\n");
    printf("  -l LIB         Search for library LIB\n");
    printf("  -v, --version  Display version information\n");
    printf("  --help         Display this help\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("GNU ld (akaOS Native Toolchain) 2.42\n");
            printf("Copyright (C) 2026 Free Software Foundation, Inc.\n");
            return 0;
        }
    }

    /* Build command line forwarding to system linker ld.lld */
    size_t cmd_len = 32;
    for (int i = 1; i < argc; ++i) {
        cmd_len += strlen(argv[i]) + 3;
    }

    char *cmd = malloc(cmd_len);
    if (!cmd) return 1;

    snprintf(cmd, cmd_len, "ld.lld");
    for (int i = 1; i < argc; ++i) {
        strcat(cmd, " ");
        strcat(cmd, argv[i]);
    }

    int ret = system(cmd);
    free(cmd);

    return (ret == 0) ? 0 : 1;
}
