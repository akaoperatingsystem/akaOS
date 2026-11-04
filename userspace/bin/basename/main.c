#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(void) {
    printf("Usage: basename NAME [SUFFIX]\n"
           "  or:  basename OPTION... NAME...\n"
           "Print NAME with any leading directory components removed.\n"
           "If specified, also remove a trailing SUFFIX.\n\n"
           "Options:\n"
           "  -a, --multiple       support multiple arguments and treat each as a NAME\n"
           "  -s, --suffix=SUFFIX  remove a trailing SUFFIX; implies -a\n"
           "  -z, --zero           end each output line with NUL, not newline\n"
           "      --help           display this help and exit\n"
           "      --version        output version information and exit\n");
}

static void print_version(void) {
    printf("basename (akaOS coreutils) 0.2.44\n");
}

/* Process a single path string and strip suffix if matching */
static void strip_basename(const char *path, const char *suffix, char term_char) {
    if (!path || !*path) {
        putchar(term_char);
        return;
    }

    size_t len = strlen(path);
    /* Remove trailing slashes (unless the string is all slashes) */
    while (len > 1 && path[len - 1] == '/') len--;

    if (len == 1 && path[0] == '/') {
        putchar('/');
        putchar(term_char);
        return;
    }

    const char *start = path;
    const char *p = path + len - 1;
    while (p >= path && *p != '/') p--;
    start = p + 1;
    size_t base_len = (size_t)((path + len) - start);

    /* Strip suffix if present and does not equal entire base */
    if (suffix && *suffix) {
        size_t slen = strlen(suffix);
        if (base_len > slen && memcmp(start + base_len - slen, suffix, slen) == 0) {
            base_len -= slen;
        }
    }

    fwrite(start, 1, base_len, stdout);
    putchar(term_char);
}

int main(int argc, char **argv) {
    int opt_multiple = 0;
    const char *opt_suffix = NULL;
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
        if (strcmp(arg, "-a") == 0 || strcmp(arg, "--multiple") == 0) {
            opt_multiple = 1;
            continue;
        }
        if (strcmp(arg, "-z") == 0 || strcmp(arg, "--zero") == 0) {
            term_char = '\0';
            continue;
        }
        if (strncmp(arg, "--suffix=", 9) == 0) {
            opt_suffix = arg + 9;
            opt_multiple = 1;
            continue;
        }
        if (strcmp(arg, "-s") == 0 && i + 1 < argc) {
            opt_suffix = argv[++i];
            opt_multiple = 1;
            continue;
        }
        if (strncmp(arg, "-s", 2) == 0 && arg[2] != '\0') {
            opt_suffix = arg + 2;
            opt_multiple = 1;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            first_operand = i + 1;
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "basename: unrecognized option '%s'\n", arg);
            fprintf(stderr, "Try 'basename --help' for more information.\n");
            return 1;
        }
        first_operand = i;
        break;
    }

    int operand_count = argc - first_operand;
    if (operand_count < 1) {
        fprintf(stderr, "basename: missing operand\n");
        fprintf(stderr, "Try 'basename --help' for more information.\n");
        return 1;
    }

    if (opt_multiple) {
        for (int i = first_operand; i < argc; ++i) {
            strip_basename(argv[i], opt_suffix, term_char);
        }
    } else {
        if (operand_count > 2) {
            fprintf(stderr, "basename: extra operand '%s'\n", argv[first_operand + 2]);
            fprintf(stderr, "Try 'basename --help' for more information.\n");
            return 1;
        }
        const char *name = argv[first_operand];
        const char *suf = (operand_count == 2) ? argv[first_operand + 1] : NULL;
        strip_basename(name, suf, term_char);
    }

    return 0;
}
