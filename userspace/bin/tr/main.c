#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint8_t set1[512];
static size_t len1 = 0;
static uint8_t set2[512];
static size_t len2 = 0;

static uint8_t trans_map[256];
static uint8_t delete_map[256];
static uint8_t squeeze_map[256];

static void print_usage(void) {
    printf("Usage: tr [OPTION]... SET1 [SET2]\n");
    printf("Translate, squeeze, and/or delete characters from standard input,\n");
    printf("writing to standard output.\n\n");
    printf("Options:\n");
    printf("  -c, -C, --complement   use the complement of SET1\n");
    printf("  -d, --delete           delete characters in SET1, do not translate\n");
    printf("  -s, --squeeze-repeats  replace sequences of repeated characters with a single occurrence\n");
    printf("  -h, --help             display this help and exit\n");
}

static size_t expand_set(const char *src, uint8_t *dest, size_t max_len) {
    size_t count = 0;
    const char *p = src;

    while (*p && count < max_len) {
        uint8_t c = (uint8_t)*p;

        if (c == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'a': c = '\a'; break;
                case 'b': c = '\b'; break;
                case 'v': c = '\v'; break;
                case 'f': c = '\f'; break;
                case '0': c = '\0'; break;
                case '\\': c = '\\'; break;
                default: c = (uint8_t)*p; break;
            }
        }

        if (*(p + 1) == '-' && *(p + 2) != '\0') {
            uint8_t start = c;
            p += 2;
            uint8_t end = (uint8_t)*p;
            if (*p == '\\' && *(p + 1)) {
                p++;
                switch (*p) {
                    case 'n': end = '\n'; break;
                    case 't': end = '\t'; break;
                    case 'r': end = '\r'; break;
                    case 'a': end = '\a'; break;
                    case 'b': end = '\b'; break;
                    case 'v': end = '\v'; break;
                    case 'f': end = '\f'; break;
                    case '0': end = '\0'; break;
                    case '\\': end = '\\'; break;
                    default: end = (uint8_t)*p; break;
                }
            }
            if (start <= end) {
                for (int val = start; val <= end && count < max_len; val++) {
                    dest[count++] = (uint8_t)val;
                }
            } else {
                dest[count++] = start;
                dest[count++] = '-';
                dest[count++] = end;
            }
        } else {
            dest[count++] = c;
        }
        p++;
    }
    return count;
}

int main(int argc, char *argv[]) {
    int opt_complement = 0;
    int opt_delete = 0;
    int opt_squeeze = 0;

    int arg_idx = 1;
    while (arg_idx < argc && argv[arg_idx][0] == '-' && argv[arg_idx][1] != '\0') {
        if (strcmp(argv[arg_idx], "-h") == 0 || strcmp(argv[arg_idx], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[arg_idx], "--") == 0) {
            arg_idx++;
            break;
        } else if (strcmp(argv[arg_idx], "-d") == 0 || strcmp(argv[arg_idx], "--delete") == 0) {
            opt_delete = 1;
        } else if (strcmp(argv[arg_idx], "-c") == 0 || strcmp(argv[arg_idx], "-C") == 0 || strcmp(argv[arg_idx], "--complement") == 0) {
            opt_complement = 1;
        } else if (strcmp(argv[arg_idx], "-s") == 0 || strcmp(argv[arg_idx], "--squeeze-repeats") == 0) {
            opt_squeeze = 1;
        } else {
            /* Combined short options like -ds, -cd, etc. */
            for (size_t j = 1; argv[arg_idx][j]; j++) {
                char ch = argv[arg_idx][j];
                if (ch == 'd') opt_delete = 1;
                else if (ch == 'c' || ch == 'C') opt_complement = 1;
                else if (ch == 's') opt_squeeze = 1;
                else {
                    fprintf(stderr, "tr: invalid option -- '%c'\n", ch);
                    print_usage();
                    return 1;
                }
            }
        }
        arg_idx++;
    }

    if (arg_idx >= argc) {
        fprintf(stderr, "tr: missing operand\n");
        fprintf(stderr, "Try 'tr --help' for more information.\n");
        return 1;
    }

    const char *str1 = argv[arg_idx++];
    const char *str2 = (arg_idx < argc) ? argv[arg_idx++] : NULL;

    len1 = expand_set(str1, set1, sizeof(set1));
    if (str2) {
        len2 = expand_set(str2, set2, sizeof(set2));
    }

    /* Initialize maps */
    for (int i = 0; i < 256; i++) {
        trans_map[i] = (uint8_t)i;
        delete_map[i] = 0;
        squeeze_map[i] = 0;
    }

    if (opt_delete) {
        /* Mark characters to delete */
        for (size_t i = 0; i < len1; i++) {
            delete_map[set1[i]] = 1;
        }
        if (opt_complement) {
            for (int i = 0; i < 256; i++) {
                delete_map[i] = !delete_map[i];
            }
        }
        if (opt_squeeze && str2) {
            for (size_t i = 0; i < len2; i++) {
                squeeze_map[set2[i]] = 1;
            }
        }
    } else if (opt_squeeze && !str2) {
        /* Squeeze only */
        for (size_t i = 0; i < len1; i++) {
            squeeze_map[set1[i]] = 1;
        }
        if (opt_complement) {
            for (int i = 0; i < 256; i++) {
                squeeze_map[i] = !squeeze_map[i];
            }
        }
    } else {
        /* Translation */
        if (!str2 || len2 == 0) {
            fprintf(stderr, "tr: missing operand after '%s'\n", str1);
            return 1;
        }

        if (opt_complement) {
            uint8_t present[256];
            memset(present, 0, sizeof(present));
            for (size_t i = 0; i < len1; i++) present[set1[i]] = 1;

            size_t c_idx = 0;
            for (int i = 0; i < 256; i++) {
                if (!present[i]) {
                    uint8_t rep = (c_idx < len2) ? set2[c_idx] : set2[len2 - 1];
                    trans_map[i] = rep;
                    c_idx++;
                }
            }
        } else {
            for (size_t i = 0; i < len1; i++) {
                uint8_t rep = (i < len2) ? set2[i] : set2[len2 - 1];
                trans_map[set1[i]] = rep;
            }
        }

        if (opt_squeeze) {
            for (size_t i = 0; i < len2; i++) {
                squeeze_map[set2[i]] = 1;
            }
        }
    }

    /* Process standard input */
    char in_buf[1024];
    char out_buf[1024];
    ssize_t n;
    int last_out = -1;

    while ((n = read(STDIN_FILENO, in_buf, sizeof(in_buf))) > 0) {
        size_t out_len = 0;
        for (ssize_t i = 0; i < n; i++) {
            uint8_t c = (uint8_t)in_buf[i];

            if (opt_delete && delete_map[c]) {
                continue;
            }

            uint8_t mapped = trans_map[c];

            if (opt_squeeze && squeeze_map[mapped] && last_out == (int)mapped) {
                continue;
            }

            out_buf[out_len++] = (char)mapped;
            last_out = (int)mapped;

            if (out_len >= sizeof(out_buf)) {
                write(STDOUT_FILENO, out_buf, out_len);
                out_len = 0;
            }
        }

        if (out_len > 0) {
            write(STDOUT_FILENO, out_buf, out_len);
        }
    }

    return 0;
}
