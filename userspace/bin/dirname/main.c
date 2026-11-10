#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    printf("Usage: dirname [OPTION] NAME...\n"
           "Output each NAME with its last non-slash component and trailing slashes\n"
           "removed; if NAME contains no /'s, output '.' (meaning the current directory).\n\n"
           "Options:\n"
           "  -z, --zero     end each output line with NUL, not newline\n"
           "      --help     display this help and exit\n"
           "      --version  output version information and exit\n\n"
           "Examples:\n"
           "  dirname /usr/bin/          -> \"/usr\"\n"
           "  dirname dir/file           -> \"dir\"\n"
           "  dirname stdio.h            -> \".\"\n");
}

static void print_version(void) {
    printf("dirname (akaOS coreutils) 0.3.49\n");
}

static void process_dirname(const char *path, char term_char) {
    if (!path || !*path) {
        putchar('.');
        putchar(term_char);
        return;
    }

    size_t len = strlen(path);

    /* 1. Strip trailing slashes (unless path is entirely slashes) */
    while (len > 1 && path[len - 1] == '/') {
        len--;
    }

    /* If it was all slashes, result is "/" */
    if (len == 1 && path[0] == '/') {
        putchar('/');
        putchar(term_char);
        return;
    }

    /* 2. Find the last slash before the end of the trimmed string */
    ssize_t last_slash = -1;
    for (ssize_t i = (ssize_t)len - 1; i >= 0; --i) {
        if (path[i] == '/') {
            last_slash = i;
            break;
        }
    }

    if (last_slash < 0) {
        /* No slash in path */
        putchar('.');
        putchar(term_char);
        return;
    }

    /* 3. Strip trailing slashes from the prefix (unless prefix is entirely slashes) */
    size_t prefix_len = (size_t)last_slash;
    while (prefix_len > 1 && path[prefix_len - 1] == '/') {
        prefix_len--;
    }

    if (prefix_len == 0) {
        /* The prefix was only the leading slash, e.g., "/foo" -> "/" */
        putchar('/');
        putchar(term_char);
        return;
    }

    fwrite(path, 1, prefix_len, stdout);
    putchar(term_char);
}

int main(int argc, char **argv) {
    char term_char = '\n';
    int first_operand = argc;

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
        if (strcmp(arg, "-z") == 0 || strcmp(arg, "--zero") == 0) {
            term_char = '\0';
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            first_operand = i + 1;
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "dirname: unrecognized option '%s'\n", arg);
            fprintf(stderr, "Try 'dirname --help' for more information.\n");
            return 1;
        }
        first_operand = i;
        break;
    }

    if (first_operand >= argc) {
        fprintf(stderr, "dirname: missing operand\n");
        fprintf(stderr, "Try 'dirname --help' for more information.\n");
        return 1;
    }

    for (int i = first_operand; i < argc; ++i) {
        process_dirname(argv[i], term_char);
    }

    return 0;
}
