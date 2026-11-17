#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <akaos/crc32.h>

#define WINDOW_SIZE 32768

static int opt_stdout = 0;
static int opt_keep = 0;
static int opt_force = 0;

static uint8_t in_buf[WINDOW_SIZE * 2];

static void inflate_stream(int fd_in, int fd_out) {
    ssize_t n;
    while ((n = read(fd_in, in_buf, sizeof(in_buf))) > 0) {
        write(fd_out, in_buf, n);
    }
}

int main(int argc, char **argv) {
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        const char *arg = argv[arg_idx];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            puts("Usage: gunzip [OPTION]... [FILE]...\nDecompress files compressed by gzip.\n\nOptions:\n  -c, --stdout      write on standard output, keep original files unchanged\n  -f, --force       force overwrite of output file\n  -k, --keep        keep (don't delete) input files\n  -h, --help        display this help and exit");
            return 0;
        } else if (strcmp(arg, "-c") == 0 || strcmp(arg, "--stdout") == 0) {
            opt_stdout = 1;
            arg_idx++;
        } else if (strcmp(arg, "-k") == 0 || strcmp(arg, "--keep") == 0) {
            opt_keep = 1;
            arg_idx++;
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--force") == 0) {
            opt_force = 1;
            arg_idx++;
        } else {
            arg_idx++;
        }
    }

    if (arg_idx >= argc) {
        inflate_stream(STDIN_FILENO, STDOUT_FILENO);
        return 0;
    }

    for (int i = arg_idx; i < argc; ++i) {
        const char *src_file = argv[i];
        int fd_in = open(src_file, O_RDONLY);
        if (fd_in < 0) continue;

        int fd_out = STDOUT_FILENO;
        if (!opt_stdout) {
            char dst_file[256];
            snprintf(dst_file, sizeof(dst_file), "%.*s", (int)(strlen(src_file) - 3), src_file);
            fd_out = open(dst_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (fd_out < 0) { close(fd_in); continue; }
        }

        inflate_stream(fd_in, fd_out);
        close(fd_in);
        if (fd_out != STDOUT_FILENO) close(fd_out);
    }
    return 0;
}
