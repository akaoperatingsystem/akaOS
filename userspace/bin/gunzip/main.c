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
#define IN_BUF_SIZE 4096
#define OUT_BUF_SIZE 4096

static int opt_stdout = 0;
static int opt_keep = 0;
static int opt_force = 0;

static uint8_t in_buf[IN_BUF_SIZE];
static size_t in_pos = 0;
static size_t in_len = 0;
static int in_fd_global = STDIN_FILENO;
static int in_eof = 0;

static uint8_t out_buf[OUT_BUF_SIZE];
static size_t out_used = 0;
static int out_fd_global = STDOUT_FILENO;

static uint8_t window[WINDOW_SIZE];
static size_t win_pos = 0;

static uint32_t bit_buf = 0;
static int bit_cnt = 0;

static uint32_t running_crc = 0;
static uint32_t total_out = 0;

static void flush_out(void) {
    if (out_used > 0) {
        write(out_fd_global, out_buf, out_used);
        out_used = 0;
    }
}

static void emit_byte(uint8_t b) {
    running_crc = akaos_crc32(running_crc, &b, 1);
    total_out++;

    window[win_pos] = b;
    win_pos = (win_pos + 1) % WINDOW_SIZE;

    out_buf[out_used++] = b;
    if (out_used >= OUT_BUF_SIZE) {
        flush_out();
    }
}

static int fill_in_buf(void) {
    if (in_pos >= in_len) {
        ssize_t n = read(in_fd_global, in_buf, sizeof(in_buf));
        if (n <= 0) {
            in_eof = 1;
            return 0;
        }
        in_len = (size_t)n;
        in_pos = 0;
    }
    return 1;
}

static uint8_t get_byte_raw(void) {
    if (in_pos >= in_len) {
        if (!fill_in_buf()) return 0;
    }
    return in_buf[in_pos++];
}

static uint32_t get_bits(int n) {
    while (bit_cnt < n) {
        if (in_pos >= in_len && !fill_in_buf()) {
            /* EOF reached */
            break;
        }
        bit_buf |= ((uint32_t)in_buf[in_pos++]) << bit_cnt;
        bit_cnt += 8;
    }
    uint32_t val = bit_buf & ((1u << n) - 1);
    bit_buf >>= n;
    bit_cnt -= n;
    return val;
}

static void align_to_byte(void) {
    bit_buf >>= (bit_cnt % 8);
    bit_cnt -= (bit_cnt % 8);
}

/* Huffman Tree implementation */
struct huffman_tree {
    int16_t table[1024]; /* Positive: symbol (leaf), Negative: left child index */
    int num_nodes;
};

static void build_canonical_huffman(struct huffman_tree *tree, const uint8_t *lens, int count) {
    int max_len = 0;
    for (int i = 0; i < count; ++i) {
        if (lens[i] > max_len) max_len = lens[i];
    }
    if (max_len > 15) max_len = 15;

    int bl_count[16];
    memset(bl_count, 0, sizeof(bl_count));
    for (int i = 0; i < count; ++i) {
        if (lens[i] > 0 && lens[i] <= 15) bl_count[lens[i]]++;
    }

    int next_code[16];
    int code = 0;
    bl_count[0] = 0;
    for (int bits = 1; bits <= 15; ++bits) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    memset(tree->table, 0, sizeof(tree->table));
    tree->num_nodes = 2; /* root is 0, children 0 (left) and 1 (right) */

    for (int n = 0; n < count; ++n) {
        int len = lens[n];
        if (len != 0) {
            int c = next_code[len]++;
            int node = 0;
            for (int bit = len - 1; bit >= 0; --bit) {
                int b = (c >> bit) & 1;
                int child_idx = (node * 2) + 1 + b;
                if (child_idx >= 1024) break;

                if (bit == 0) {
                    tree->table[child_idx] = (int16_t)n; /* Leaf node */
                } else {
                    if (tree->table[child_idx] == 0) {
                        /* Allocate internal node */
                        tree->table[child_idx] = (int16_t)(-tree->num_nodes);
                        node = tree->num_nodes++;
                    } else {
                        node = -tree->table[child_idx];
                    }
                }
            }
        }
    }
}

static int decode_symbol(const struct huffman_tree *tree) {
    int node = 0;
    while (1) {
        int bit = (int)get_bits(1);
        int child_idx = (node * 2) + 1 + bit;
        if (child_idx >= 1024) return -1;
        int16_t val = tree->table[child_idx];
        if (val >= 0) {
            return (int)val; /* Leaf */
        }
        node = -val;
    }
}

/* Predefined fixed Huffman tables */
static struct huffman_tree fixed_lit_tree;
static struct huffman_tree fixed_dist_tree;
static int fixed_trees_built = 0;

static void init_fixed_trees(void) {
    if (fixed_trees_built) return;

    uint8_t lit_lens[288];
    for (int i = 0; i <= 143; ++i) lit_lens[i] = 8;
    for (int i = 144; i <= 255; ++i) lit_lens[i] = 9;
    for (int i = 256; i <= 279; ++i) lit_lens[i] = 7;
    for (int i = 280; i <= 287; ++i) lit_lens[i] = 8;
    build_canonical_huffman(&fixed_lit_tree, lit_lens, 288);

    uint8_t dist_lens[32];
    for (int i = 0; i < 32; ++i) dist_lens[i] = 5;
    build_canonical_huffman(&fixed_dist_tree, dist_lens, 32);

    fixed_trees_built = 1;
}

/* Length & distance decoding tables */
static const uint16_t length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};

static const uint16_t dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577
};
static const uint8_t dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static const uint8_t clen_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static int decompress_huffman_block(const struct huffman_tree *lit_tree, const struct huffman_tree *dist_tree) {
    while (1) {
        int sym = decode_symbol(lit_tree);
        if (sym < 0) return -1;
        if (sym < 256) {
            emit_byte((uint8_t)sym);
        } else if (sym == 256) {
            break; /* End of block */
        } else {
            int len_idx = sym - 257;
            if (len_idx >= 29) return -1;
            uint32_t len = length_base[len_idx] + get_bits(length_extra[len_idx]);

            int dist_idx = decode_symbol(dist_tree);
            if (dist_idx < 0 || dist_idx >= 30) return -1;
            uint32_t dist = dist_base[dist_idx] + get_bits(dist_extra[dist_idx]);

            if (dist > WINDOW_SIZE) return -1;

            size_t src_pos = (win_pos + WINDOW_SIZE - (size_t)dist) % WINDOW_SIZE;
            for (uint32_t k = 0; k < len; ++k) {
                uint8_t b = window[src_pos];
                src_pos = (src_pos + 1) % WINDOW_SIZE;
                emit_byte(b);
            }
        }
    }
    return 0;
}

static int inflate_stream(int fd_in, int fd_out) {
    in_fd_global = fd_in;
    out_fd_global = fd_out;
    in_pos = 0;
    in_len = 0;
    in_eof = 0;
    out_used = 0;
    win_pos = 0;
    bit_buf = 0;
    bit_cnt = 0;
    running_crc = 0;
    total_out = 0;

    init_fixed_trees();

    /* Read and validate 10-byte Gzip Header */
    uint8_t id1 = get_byte_raw();
    uint8_t id2 = get_byte_raw();
    if (id1 != 0x1f || id2 != 0x8b) {
        fprintf(stderr, "gunzip: not in gzip format\n");
        return -1;
    }
    uint8_t cm = get_byte_raw();
    if (cm != 8) {
        fprintf(stderr, "gunzip: unknown compression method %d\n", cm);
        return -1;
    }
    uint8_t flg = get_byte_raw();
    /* Skip MTIME (4), XFL (1), OS (1) */
    for (int i = 0; i < 6; ++i) get_byte_raw();

    /* Skip optional extra headers */
    if (flg & 4) { /* FEXTRA */
        uint16_t xlen = (uint16_t)get_byte_raw();
        xlen |= ((uint16_t)get_byte_raw()) << 8;
        while (xlen--) get_byte_raw();
    }
    if (flg & 8) { /* FNAME */
        while (get_byte_raw() != 0) {}
    }
    if (flg & 16) { /* FCOMMENT */
        while (get_byte_raw() != 0) {}
    }
    if (flg & 2) { /* FHCRC */
        get_byte_raw();
        get_byte_raw();
    }

    int bfinal = 0;
    while (!bfinal) {
        bfinal = (int)get_bits(1);
        int btype = (int)get_bits(2);

        if (btype == 0) {
            /* Stored / uncompressed block */
            align_to_byte();
            uint16_t len = (uint16_t)get_bits(16);
            (void)get_bits(16); /* NLEN */
            for (uint16_t i = 0; i < len; ++i) {
                emit_byte((uint8_t)get_bits(8));
            }
        } else if (btype == 1) {
            /* Fixed Huffman block */
            if (decompress_huffman_block(&fixed_lit_tree, &fixed_dist_tree) != 0) {
                fprintf(stderr, "gunzip: corrupted fixed Huffman block\n");
                return -1;
            }
        } else if (btype == 2) {
            /* Dynamic Huffman block */
            int hlit = (int)get_bits(5) + 257;
            int hdist = (int)get_bits(5) + 1;
            int hclen = (int)get_bits(4) + 4;

            uint8_t clen_lens[19];
            memset(clen_lens, 0, sizeof(clen_lens));
            for (int i = 0; i < hclen; ++i) {
                clen_lens[clen_order[i]] = (uint8_t)get_bits(3);
            }

            struct huffman_tree clen_tree;
            build_canonical_huffman(&clen_tree, clen_lens, 19);

            uint8_t dynamic_lens[320];
            int total_codes = hlit + hdist;
            int d_idx = 0;

            while (d_idx < total_codes) {
                int s = decode_symbol(&clen_tree);
                if (s < 0) return -1;
                if (s <= 15) {
                    dynamic_lens[d_idx++] = (uint8_t)s;
                } else if (s == 16) {
                    int rep = (int)get_bits(2) + 3;
                    uint8_t prev_val = d_idx > 0 ? dynamic_lens[d_idx - 1] : 0;
                    while (rep-- > 0 && d_idx < total_codes) dynamic_lens[d_idx++] = prev_val;
                } else if (s == 17) {
                    int rep = (int)get_bits(3) + 3;
                    while (rep-- > 0 && d_idx < total_codes) dynamic_lens[d_idx++] = 0;
                } else if (s == 18) {
                    int rep = (int)get_bits(7) + 11;
                    while (rep-- > 0 && d_idx < total_codes) dynamic_lens[d_idx++] = 0;
                }
            }

            struct huffman_tree dyn_lit_tree;
            struct huffman_tree dyn_dist_tree;
            build_canonical_huffman(&dyn_lit_tree, dynamic_lens, hlit);
            build_canonical_huffman(&dyn_dist_tree, dynamic_lens + hlit, hdist);

            if (decompress_huffman_block(&dyn_lit_tree, &dyn_dist_tree) != 0) {
                fprintf(stderr, "gunzip: corrupted dynamic Huffman block\n");
                return -1;
            }
        } else {
            fprintf(stderr, "gunzip: invalid block type %d\n", btype);
            return -1;
        }
    }

    flush_out();

    /* Align to byte boundary before reading 8-byte trailer */
    align_to_byte();
    uint32_t exp_crc = get_bits(32);
    uint32_t exp_isize = get_bits(32);

    if (exp_crc != running_crc) {
        fprintf(stderr, "gunzip: CRC error (expected %08x, got %08x)\n", exp_crc, running_crc);
        return -1;
    }
    (void)exp_isize;

    return 0;
}

int main(int argc, char **argv) {
    int arg_idx = 1;

    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        const char *arg = argv[arg_idx];
        if (strcmp(arg, "-c") == 0 || strcmp(arg, "--stdout") == 0) {
            opt_stdout = 1;
            arg_idx++;
        } else if (strcmp(arg, "-k") == 0 || strcmp(arg, "--keep") == 0) {
            opt_keep = 1;
            arg_idx++;
        } else if (strcmp(arg, "-f") == 0 || strcmp(arg, "--force") == 0) {
            opt_force = 1;
            arg_idx++;
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            printf("Usage: gunzip [OPTION]... [FILE]...\n"
                   "Decompress FILEs (by default, in-place).\n\n"
                   "Options:\n"
                   "  -c, --stdout      write on standard output, keep original files unchanged\n"
                   "  -f, --force       force overwrite of output file\n"
                   "  -k, --keep        keep (don't delete) input files\n"
                   "  -h, --help        display this help and exit\n");
            return 0;
        } else {
            fprintf(stderr, "gunzip: unrecognized option '%s'\n", arg);
            return 2;
        }
    }

    if (arg_idx >= argc) {
        if (inflate_stream(STDIN_FILENO, STDOUT_FILENO) != 0) return 1;
        return 0;
    }

    for (int i = arg_idx; i < argc; ++i) {
        const char *src_file = argv[i];
        if (strcmp(src_file, "-") == 0) {
            if (inflate_stream(STDIN_FILENO, STDOUT_FILENO) != 0) return 1;
            continue;
        }

        int fd_in = open(src_file, O_RDONLY);
        if (fd_in < 0) {
            fprintf(stderr, "gunzip: %s: No such file or directory\n", src_file);
            continue;
        }

        int fd_out = STDOUT_FILENO;
        char dst_file[256];
        if (!opt_stdout) {
            size_t slen = strlen(src_file);
            if (slen > 3 && strcmp(src_file + slen - 3, ".gz") == 0) {
                strncpy(dst_file, src_file, slen - 3);
                dst_file[slen - 3] = '\0';
            } else {
                snprintf(dst_file, sizeof(dst_file), "%s.out", src_file);
            }

            if (!opt_force) {
                int check = open(dst_file, O_RDONLY);
                if (check >= 0) {
                    close(check);
                    fprintf(stderr, "gunzip: %s already exists; use -f to overwrite\n", dst_file);
                    close(fd_in);
                    continue;
                }
            }

            fd_out = open(dst_file, O_WRONLY | O_CREAT | O_TRUNC);
            if (fd_out < 0) {
                fprintf(stderr, "gunzip: cannot create %s\n", dst_file);
                close(fd_in);
                continue;
            }
        }

        int res = inflate_stream(fd_in, fd_out);
        close(fd_in);
        if (fd_out != STDOUT_FILENO) close(fd_out);

        if (res == 0 && !opt_stdout && !opt_keep) {
            unlink(src_file);
        }
    }

    return 0;
}
