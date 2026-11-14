#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#define DEFAULT_WIDTH 80

static int opt_bytes = 0;   /* -b */
static int opt_spaces = 0;  /* -s */
static int opt_width = DEFAULT_WIDTH; /* -w */

static void print_usage(void) {
    printf("Usage: fold [OPTION]... [FILE]...\n"
           "Wrap input lines to fit in specified width.\n\n"
           "Options:\n"
           "  -b, --bytes           count bytes rather than columns\n"
           "  -s, --spaces          break at word boundaries\n"
           "  -w, --width=WIDTH     use WIDTH columns instead of 80\n"
           "  -h, --help            display this help and exit\n");
}

static void fold_file(FILE *f) {
    int c;
    int col = 0;
    int last_space = -1;
    char buffer[4096];
    int buf_idx = 0;

    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') {
            buffer[buf_idx++] = c;
            fwrite(buffer, 1, buf_idx, stdout);
            buf_idx = 0;
            col = 0;
            last_space = -1;
            continue;
        }

        if (!opt_bytes) {
            if (c == '\t') col = (col + 8) & ~7;
            else if (c == '\b') col = (col > 0) ? col - 1 : 0;
            else if (c == '\r') col = 0;
            else col++;
        } else {
            col++;
        }

        buffer[buf_idx++] = c;

        if (opt_spaces && (c == ' ' || c == '\t')) {
            last_space = buf_idx - 1;
        }

        if (col >= opt_width) {
            if (opt_spaces && last_space != -1) {
                fwrite(buffer, 1, last_space + 1, stdout);
                printf("\n");
                int remaining = buf_idx - (last_space + 1);
                memmove(buffer, buffer + last_space + 1, remaining);
                buf_idx = remaining;
                col = 0; // Simplified for this implementation
                last_space = -1;
            } else {
                fwrite(buffer, 1, buf_idx, stdout);
                printf("\n");
                buf_idx = 0;
                col = 0;
                last_space = -1;
            }
        }
    }
    if (buf_idx > 0) {
        fwrite(buffer, 1, buf_idx, stdout);
    }
}

int main(int argc, char **argv) {
    int opt;
    static struct option long_options[] = {
        {"bytes", no_argument, 0, 'b'},
        {"spaces", no_argument, 0, 's'},
        {"width", required_argument, 0, 'w'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "bsw:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'b': opt_bytes = 1; break;
            case 's': opt_spaces = 1; break;
            case 'w': opt_width = atoi(optarg); break;
            case 'h': print_usage(); return 0;
            default: print_usage(); return 1;
        }
    }

    if (optind == argc) {
        fold_file(stdin);
    } else {
        for (int i = optind; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (f) {
                fold_file(f);
                fclose(f);
            } else {
                fprintf(stderr, "fold: %s: No such file\n", argv[i]);
            }
        }
    }
    return 0;
}
