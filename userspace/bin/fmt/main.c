#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#define DEFAULT_WIDTH 75

static int opt_width = DEFAULT_WIDTH;
static int opt_split_only = 0;
static int opt_uniform = 0;

static void print_usage(void) {
    printf("Usage: fmt [OPTION]... [FILE]...\n"
           "Reformat paragraph text.\n\n"
           "Options:\n"
           "  -w, --width=WIDTH   fill output lines up to WIDTH characters (default 75)\n"
           "  -s, --split-only    split lines only, do not join short lines\n"
           "  -u, --uniform-spacing uniform spacing (one space between words, two between sentences)\n"
           "  -h, --help          display this help and exit\n");
}

static void format_paragraph(char **lines, int n) {
    int current_len = 0;
    for (int i = 0; i < n; i++) {
        char *line = lines[i];
        int len = strlen(line);
        if (i > 0) {
            if (current_len + 1 + len <= opt_width) {
                printf(" ");
                current_len += 1;
            } else {
                printf("\n");
                current_len = 0;
            }
        }
        printf("%s", line);
        current_len += len;
    }
    printf("\n");
}

static void process_file(FILE *f) {
    char *line = NULL;
    size_t len = 0;
    char **paragraph = NULL;
    int n = 0;

    while (getline(&line, &len, f) != -1) {
        if (line[0] == '\n' || line[0] == '\r') {
            if (n > 0) {
                format_paragraph(paragraph, n);
                for (int i = 0; i < n; i++) free(paragraph[i]);
                free(paragraph);
                paragraph = NULL;
                n = 0;
            }
            printf("\n");
        } else {
            line[strcspn(line, "\r\n")] = 0;
            paragraph = realloc(paragraph, sizeof(char *) * (n + 1));
            char *words = strtok(line, " ");
            while (words) {
                paragraph = realloc(paragraph, sizeof(char *) * (n + 1));
                paragraph[n++] = strdup(words);
                words = strtok(NULL, " ");
            }
        }
    }
    if (n > 0) {
        format_paragraph(paragraph, n);
        for (int i = 0; i < n; i++) free(paragraph[i]);
        free(paragraph);
    }
    free(line);
}

int main(int argc, char **argv) {
    int i = 1;
    for (; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 || strncmp(argv[i], "--width=", 8) == 0) {
            if (argv[i][1] == 'w') opt_width = atoi(argv[++i]);
            else opt_width = atoi(argv[i] + 8);
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--split-only") == 0) {
            opt_split_only = 1;
        } else if (strcmp(argv[i], "-u") == 0 || strcmp(argv[i], "--uniform-spacing") == 0) {
            opt_uniform = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (argv[i][0] == '-') {
             // Handle simple -WIDTH
             if (isdigit(argv[i][1])) opt_width = atoi(argv[i] + 1);
        } else {
            break;
        }
    }

    if (i == argc) {
        process_file(stdin);
    } else {
        for (; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (f) {
                process_file(f);
                fclose(f);
            } else {
                fprintf(stderr, "fmt: %s: %s\n", argv[i], "No such file");
            }
        }
    }
    return 0;
}
