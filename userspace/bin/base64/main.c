#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 4096

static const char b64_alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void print_usage(void) {
    printf("Usage: base64 [OPTION]... [FILE]\n"
           "Base64 encode or decode FILE, or standard input, to standard output.\n\n"
           "With no FILE, or when FILE is -, read standard input.\n\n"
           "  -d, --decode          decode data\n"
           "  -i, --ignore-garbage  when decoding, ignore non-alphabet characters\n"
           "  -w, --wrap=COLS       wrap encoded lines after COLS character (default 76).\n"
           "                        Use 0 to disable line wrapping\n"
           "      --help            display this help and exit\n"
           "      --version         output version information and exit\n");
}

static void print_version(void) {
    printf("base64 (akaOS coreutils) 0.2.49\n");
}

static void base64_encode_chunk(const uint8_t *in, size_t in_len, char *out) {
    uint8_t b[3] = {0};
    for (size_t i = 0; i < in_len; ++i) b[i] = in[i];

    out[0] = b64_alphabet[(b[0] >> 2) & 0x3f];
    out[1] = b64_alphabet[((b[0] << 4) & 0x30) | ((b[1] >> 4) & 0x0f)];
    out[2] = (in_len > 1) ? b64_alphabet[((b[1] << 2) & 0x3c) | ((b[2] >> 6) & 0x03)] : '=';
    out[3] = (in_len > 2) ? b64_alphabet[b[2] & 0x3f] : '=';
    out[4] = '\0';
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64_decode_block(const char *in, uint8_t *out, size_t *out_len) {
    int v[4];
    int pad_start = 4;
    for (int i = 0; i < 4; ++i) {
        if (in[i] == '=') {
            if (pad_start == 4) pad_start = i;
            v[i] = 0;
        } else {
            v[i] = b64_val(in[i]);
            if (v[i] < 0) return -1;
        }
    }

    out[0] = (uint8_t)((v[0] << 2) | (v[1] >> 4));
    out[1] = (uint8_t)((v[1] << 4) | (v[2] >> 2));
    out[2] = (uint8_t)((v[2] << 6) | v[3]);

    if (pad_start == 2) *out_len = 1;
    else if (pad_start == 3) *out_len = 2;
    else if (pad_start == 4) *out_len = 3;
    else return -1;

    return 0;
}

static int do_encode(int fd, int wrap_cols) {
    uint8_t in_buf[BUF_SIZE];
    uint8_t pending[3];
    size_t pending_len = 0;
    int col = 0;

    for (;;) {
        ssize_t n = read(fd, in_buf, sizeof(in_buf));
        if (n < 0) {
            fprintf(stderr, "base64: read error: %s\n", strerror(errno));
            return 1;
        }
        if (n == 0) break;

        size_t offset = 0;
        while (offset < (size_t)n) {
            while (pending_len < 3 && offset < (size_t)n) {
                pending[pending_len++] = in_buf[offset++];
            }
            if (pending_len == 3) {
                char out_block[5];
                base64_encode_chunk(pending, 3, out_block);
                for (int i = 0; i < 4; ++i) {
                    putchar(out_block[i]);
                    col++;
                    if (wrap_cols > 0 && col >= wrap_cols) {
                        putchar('\n');
                        col = 0;
                    }
                }
                pending_len = 0;
            }
        }
    }

    if (pending_len > 0) {
        char out_block[5];
        base64_encode_chunk(pending, pending_len, out_block);
        for (int i = 0; i < 4; ++i) {
            putchar(out_block[i]);
            col++;
            if (wrap_cols > 0 && col >= wrap_cols) {
                putchar('\n');
                col = 0;
            }
        }
    }

    if (wrap_cols > 0 && col > 0) {
        putchar('\n');
    } else if (wrap_cols == 0 && col > 0) {
        putchar('\n');
    }

    return 0;
}

static int do_decode(int fd, int ignore_garbage) {
    char block[4];
    size_t block_len = 0;
    char in_buf[BUF_SIZE];

    for (;;) {
        ssize_t n = read(fd, in_buf, sizeof(in_buf));
        if (n < 0) {
            fprintf(stderr, "base64: read error: %s\n", strerror(errno));
            return 1;
        }
        if (n == 0) break;

        for (ssize_t i = 0; i < n; ++i) {
            char c = in_buf[i];
            if (c == '\r' || c == '\n' || c == ' ' || c == '\t') {
                continue;
            }

            if (b64_val(c) >= 0 || c == '=') {
                block[block_len++] = c;
            } else if (!ignore_garbage) {
                fprintf(stderr, "base64: invalid input\n");
                return 1;
            }

            if (block_len == 4) {
                uint8_t out[3];
                size_t out_len = 0;
                if (base64_decode_block(block, out, &out_len) != 0) {
                    fprintf(stderr, "base64: invalid input\n");
                    return 1;
                }
                fwrite(out, 1, out_len, stdout);
                block_len = 0;
            }
        }
    }

    if (block_len > 0) {
        fprintf(stderr, "base64: invalid input\n");
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    int opt_decode = 0;
    int opt_ignore_garbage = 0;
    int opt_wrap = 76;
    const char *filename = NULL;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-V") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-d") == 0 || strcmp(arg, "-D") == 0 || strcmp(arg, "--decode") == 0) {
            opt_decode = 1;
            continue;
        }
        if (strcmp(arg, "-i") == 0 || strcmp(arg, "--ignore-garbage") == 0) {
            opt_ignore_garbage = 1;
            continue;
        }
        if (strncmp(arg, "--wrap=", 7) == 0) {
            opt_wrap = atoi(arg + 7);
            continue;
        }
        if (strcmp(arg, "-w") == 0 && i + 1 < argc) {
            opt_wrap = atoi(argv[++i]);
            continue;
        }
        if (strncmp(arg, "-w", 2) == 0 && arg[2] != '\0') {
            opt_wrap = atoi(arg + 2);
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            if (i + 1 < argc) filename = argv[++i];
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "base64: unrecognized option '%s'\n", arg);
            fprintf(stderr, "Try 'base64 --help' for more information.\n");
            return 1;
        }
        filename = arg;
    }

    int fd = STDIN_FILENO;
    if (filename && strcmp(filename, "-") != 0) {
        fd = open(filename, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "base64: %s: %s\n", filename, strerror(errno));
            return 1;
        }
    }

    int ret = 0;
    if (opt_decode) {
        ret = do_decode(fd, opt_ignore_garbage);
    } else {
        ret = do_encode(fd, opt_wrap);
    }

    if (fd != STDIN_FILENO) {
        close(fd);
    }

    return ret;
}
