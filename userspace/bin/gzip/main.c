#include <fcntl.h>
#include <akaos/crc32.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WINDOW_SIZE 32768
#define HASH_SIZE 4096
#define MAX_MATCH 258
#define MIN_MATCH 3

static int opt_stdout = 0;
static int opt_keep = 0;
static int opt_force = 0;

static uint8_t in_buf[WINDOW_SIZE * 2];
static uint8_t out_buf[WINDOW_SIZE];
static size_t out_used = 0;

static uint32_t bit_buffer = 0;
static int bit_count = 0;

static int out_fd_global = STDOUT_FILENO;

static void flush_out_buf(void) {
    if (out_used > 0) {
        write(out_fd_global, out_buf, out_used);
        out_used = 0;
    }
}

static void put_byte(uint8_t b) {
    out_buf[out_used++] = b;
    if (out_used >= sizeof(out_buf)) {
        flush_out_buf();
    }
}

static void send_bits(uint32_t val, int count) {
    bit_buffer |= (val << bit_count);
    bit_count += count;
    while (bit_count >= 8) {
        put_byte((uint8_t)(bit_buffer & 0xFF));
        bit_buffer >>= 8;
        bit_count -= 8;
    }
}

static void flush_bits(void) {
    if (bit_count > 0) {
        put_byte((uint8_t)(bit_buffer & 0xFF));
        bit_buffer = 0;
        bit_count = 0;
    }
}

static uint32_t bit_reverse(uint32_t code, int len) {
    uint32_t res = 0;
    for (int i = 0; i < len; ++i) {
        res = (res << 1) | (code & 1);
        code >>= 1;
    }
    return res;
}

static void send_fixed_literal(uint32_t sym) {
    uint32_t code;
    int len;
    if (sym <= 143) {
        code = 0x30 + sym;
        len = 8;
    } else if (sym <= 255) {
        code = 0x190 + (sym - 144);
        len = 9;
    } else if (sym <= 279) {
        code = sym - 256;
        len = 7;
    } else {
        code = 0xC0 + (sym - 280);
        len = 8;
    }
    send_bits(bit_reverse(code, len), len);
}

/* Length code table */
static const uint16_t length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

static void send_length(uint32_t len) {
    for (int i = 28; i >= 0; --i) {
        if (len >= length_base[i]) {
            uint32_t sym = 257 + (uint32_t)i;
            send_fixed_literal(sym);
            int extra = length_extra[i];
            if (extra > 0) {
                send_bits(len - length_base[i], extra);
            }
            return;
        }
    }
}

/* Distance code table */
static const uint16_t dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const uint8_t dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static void send_distance(uint32_t dist) {
    for (int i = 29; i >= 0; --i) {
        if (dist >= dist_base[i]) {
            send_bits(bit_reverse((uint32_t)i, 5), 5);
            int extra = dist_extra[i];
            if (extra > 0) {
                send_bits(dist - dist_base[i], extra);
            }
            return;
        }
    }
}

/* LZ77 match search hash table */
static int16_t head[HASH_SIZE];
static int16_t prev[WINDOW_SIZE];

static uint32_t hash3(const uint8_t *p) {
    return ((uint32_t)p[0] ^ ((uint32_t)p[1] << 4) ^ ((uint32_t)p[2] << 8)) % HASH_SIZE;
}

static void deflate_stream(int fd_in, int fd_out, uint32_t *out_crc, uint32_t *out_isize) {
    out_fd_global = fd_out;
    out_used = 0;
    bit_buffer = 0;
    bit_count = 0;

    uint32_t running_crc = 0;
    uint32_t total_in = 0;

    /* Write 10-byte Gzip Header */
    put_byte(0x1f);
    put_byte(0x8b);
    put_byte(0x08); /* DEFLATE */
    put_byte(0x00); /* FLG = 0 */
    put_byte(0x00); put_byte(0x00); put_byte(0x00); put_byte(0x00); /* MTIME = 0 */
    put_byte(0x00); /* XFL = 0 */
    put_byte(0x03); /* OS = Unix */

    /* Deflate block header: BFINAL = 1, BTYPE = 01 (fixed Huffman) */
    send_bits(1, 1);
    send_bits(1, 2);

    /* Read all data into memory for compression (stream blocks up to WINDOW_SIZE) */
    size_t in_len = 0;
    while (in_len < sizeof(in_buf)) {
        ssize_t n = read(fd_in, in_buf + in_len, sizeof(in_buf) - in_len);
        if (n <= 0) break;
        in_len += (size_t)n;
    }

    running_crc = akaos_crc32(0, in_buf, in_len);
    total_in = (uint32_t)in_len;

    /* Initialize LZ77 hash tables */
    for (int h = 0; h < HASH_SIZE; ++h) head[h] = -1;

    size_t pos = 0;
    while (pos < in_len) {
        uint32_t match_len = 0;
        uint32_t match_dist = 0;

        if (pos + MIN_MATCH <= in_len) {
            uint32_t h = hash3(&in_buf[pos]);
            int match_pos = head[h];
            prev[pos % WINDOW_SIZE] = (int16_t)match_pos;
            head[h] = (int16_t)(pos % WINDOW_SIZE);

            int chain_len = 32;
            while (match_pos != -1 && chain_len-- > 0) {
                int dist = (int)(pos % WINDOW_SIZE) - match_pos;
                if (dist <= 0) dist += WINDOW_SIZE;

                if (dist > 0 && (size_t)dist <= pos && dist < WINDOW_SIZE) {
                    size_t m_pos = pos - (size_t)dist;
                    uint32_t l = 0;
                    while (pos + l < in_len && l < MAX_MATCH && in_buf[pos + l] == in_buf[m_pos + l]) {
                        l++;
                    }
                    if (l > match_len && l >= MIN_MATCH) {
                        match_len = l;
                        match_dist = (uint32_t)dist;
                        if (match_len >= MAX_MATCH) break;
                    }
                }
                match_pos = prev[match_pos];
            }
        }

        if (match_len >= MIN_MATCH) {
            send_length(match_len);
            send_distance(match_dist);
            for (size_t k = 1; k < match_len && pos + k + MIN_MATCH <= in_len; ++k) {
                uint32_t h = hash3(&in_buf[pos + k]);
                prev[(pos + k) % WINDOW_SIZE] = head[h];
                head[h] = (int16_t)((pos + k) % WINDOW_SIZE);
            }
            pos += match_len;
        } else {
            send_fixed_literal(in_buf[pos++]);
        }
    }

    /* Send end of block symbol 256 */
    send_fixed_literal(256);
    flush_bits();

    /* Write 8-byte trailer: CRC32 + ISIZE (little endian) */
    put_byte((uint8_t)(running_crc & 0xFF));
    put_byte((uint8_t)((running_crc >> 8) & 0xFF));
    put_byte((uint8_t)((running_crc >> 16) & 0xFF));
    put_byte((uint8_t)((running_crc >> 24) & 0xFF));

    put_byte((uint8_t)(total_in & 0xFF));
    put_byte((uint8_t)((total_in >> 8) & 0xFF));
    put_byte((uint8_t)((total_in >> 16) & 0xFF));
    put_byte((uint8_t)((total_in >> 24) & 0xFF));

    flush_out_buf();

    if (out_crc) *out_crc = running_crc;
    if (out_isize) *out_isize = total_in;
}

int main(int argc, char **argv) {
    int arg_idx = 1;
    int opt_decompress = 0;

    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        const char *arg = argv[arg_idx];
        if (strcmp(arg, "-c") == 0 || strcmp(arg, "--stdout") == 0) {
            opt_stdout = 1;
            arg_idx++;
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--decompress") == 0) {
            opt_decompress = 1;
            arg_idx++;
        } else if (strcmp(arg, "-k") == 0 || strcmp(arg, "--keep") == 0) {
            opt_keep = 1;
            arg_idx++;
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--force") == 0) {
            opt_force = 1;
            arg_idx++;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            printf("Usage: gzip [OPTION]... [FILE]...\n"
                   "Compress or uncompress FILEs (by default, compress FILES in-place).\n\n"
                   "Options:\n"
                   "  -c, --stdout      write on standard output, keep original files unchanged\n"
                   "  -d, --decompress  decompress\n"
                   "  -f, --force       force overwrite of output file and compress links\n"
                   "  -k, --keep        keep (don't delete) input files\n"
                   "  -h, --help        display this help and exit\n");
            return 0;
        } else {
            fprintf(stderr, "gzip: unrecognized option '%s'\n", arg);
            return 2;
        }
    }

    if (opt_decompress) {
        /* Forward to gunzip command */
        char *gunzip_args[16];
        int g_argc = 0;
        gunzip_args[g_argc++] = "/bin/gunzip";
        if (opt_stdout) gunzip_args[g_argc++] = "-c";
        if (opt_keep) gunzip_args[g_argc++] = "-k";
        if (opt_force) gunzip_args[g_argc++] = "-f";
        while (arg_idx < argc && g_argc < 15) {
            gunzip_args[g_argc++] = argv[arg_idx++];
        }
        gunzip_args[g_argc] = NULL;
        execv("/bin/gunzip", gunzip_args);
        return 1;
    }

    if (arg_idx >= argc) {
        deflate_stream(STDIN_FILENO, STDOUT_FILENO, NULL, NULL);
        return 0;
    }

    for (int i = arg_idx; i < argc; ++i) {
        const char *src_file = argv[i];
        if (strcmp(src_file, "-") == 0) {
            deflate_stream(STDIN_FILENO, STDOUT_FILENO, NULL, NULL);
            continue;
        }

        int fd_in = open(src_file, O_RDONLY);
        if (fd_in < 0) {
            fprintf(stderr, "gzip: %s: No such file or directory\n", src_file);
            continue;
        }

        int fd_out = STDOUT_FILENO;
        char dst_file[256];
        if (!opt_stdout) {
            snprintf(dst_file, sizeof(dst_file), "%s.gz", src_file);
            if (!opt_force) {
                int check = open(dst_file, O_RDONLY);
                if (check >= 0) {
                    close(check);
                    fprintf(stderr, "gzip: %s already exists; use -f to overwrite\n", dst_file);
                    close(fd_in);
                    continue;
                }
            }
            fd_out = open(dst_file, O_WRONLY | O_CREAT | O_TRUNC);
            if (fd_out < 0) {
                fprintf(stderr, "gzip: cannot create %s\n", dst_file);
                close(fd_in);
                continue;
            }
        }

        deflate_stream(fd_in, fd_out, NULL, NULL);
        close(fd_in);
        if (fd_out != STDOUT_FILENO) close(fd_out);

        if (!opt_stdout && !opt_keep) {
            unlink(src_file);
        }
    }

    return 0;
}
