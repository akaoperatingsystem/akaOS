#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

static int tab_stops[100];
static int tab_count = 0;
static int initial_only = 0;

static void expand_line(const char *line) {
    int col = 0;
    int in_initial = 1;
    for (const char *p = line; *p; p++) {
        if (*p == '\t') {
            if (initial_only && !in_initial) {
                putchar('\t');
                col++;
                continue;
            }
            int next_tab = 8;
            if (tab_count == 1) {
                next_tab = tab_stops[0] - (col % tab_stops[0]);
            } else if (tab_count > 1) {
                int found = 0;
                for (int i = 0; i < tab_count; i++) {
                    if (tab_stops[i] > col) {
                        next_tab = tab_stops[i] - col;
                        found = 1;
                        break;
                    }
                }
                if (!found) next_tab = 1;
            }
            
            for (int i = 0; i < next_tab; i++) putchar(' ');
            col += next_tab;
        } else {
            if (*p != ' ' && *p != '\t') in_initial = 0;
            putchar(*p);
            if (*p == '\b') {
                if (col > 0) col--;
            } else {
                col++;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int opt;
    static struct option long_options[] = {
        {"tabs", required_argument, 0, 't'},
        {"initial", no_argument, 0, 'i'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "t:ih", long_options, NULL)) != -1) {
        switch (opt) {
            case 't': {
                char *s = optarg;
                while (s && *s) {
                    tab_stops[tab_count++] = atoi(s);
                    s = strchr(s, ',');
                    if (s) s++;
                }
                break;
            }
            case 'i': initial_only = 1; break;
            case 'h': printf("Usage: expand [options]... [file]...\n"); return 0;
            default: return 1;
        }
    }

    if (tab_count == 0) tab_stops[tab_count++] = 8;

    if (optind == argc) {
        char line[4096];
        while (fgets(line, sizeof(line), stdin)) expand_line(line);
    } else {
        for (int i = optind; i < argc; i++) {
            if (strcmp(argv[i], "-") == 0) {
                char line[4096];
                while (fgets(line, sizeof(line), stdin)) expand_line(line);
            } else {
                FILE *f = fopen(argv[i], "r");
                if (f) {
                    char line[4096];
                    while (fgets(line, sizeof(line), f)) expand_line(line);
                    fclose(f);
                } else {
                    perror(argv[i]);
                }
            }
        }
    }
    return 0;
}
