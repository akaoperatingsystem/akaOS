#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int opt_changes = 0;       /* -c, --changes */
static int opt_quiet = 0;         /* -f, --silent, --quiet */
static int opt_verbose = 0;       /* -v, --verbose */
static int opt_recursive = 0;     /* -R, --recursive */

static char ref_file[256] = "";
static int has_reference = 0;

static void print_usage(void) {
    printf("Usage: chmod [OPTION]... MODE[,MODE]... FILE...\n"
           "       chmod [OPTION]... OCTAL-MODE FILE...\n"
           "       chmod [OPTION]... --reference=RFILE FILE...\n"
           "Change the mode of each FILE to MODE.\n\n"
           "  -c, --changes          like verbose but report only when a change is made\n"
           "  -f, --silent, --quiet  suppress most error messages\n"
           "  -v, --verbose          output a diagnostic for every file processed\n"
           "  -R, --recursive        change files and directories recursively\n"
           "      --reference=RFILE  use RFILE's mode instead of MODE values\n"
           "      --help             display this help and exit\n"
           "      --version          output version information and exit\n");
}

static void print_version(void) {
    printf("chmod (akaOS coreutils) 0.2.91\n");
}

static int parse_octal_mode(const char *str, mode_t *out_mode) {
    if (!str || !*str) return 0;
    mode_t mode = 0;
    for (size_t i = 0; str[i]; ++i) {
        if (str[i] < '0' || str[i] > '7') return 0;
        mode = (mode << 3) | (mode_t)(str[i] - '0');
    }
    if (out_mode) *out_mode = mode;
    return 1;
}

static int parse_symbolic_mode(const char *str, mode_t current_mode, mode_t *out_mode) {
    if (!str || !*str) return 0;
    mode_t mode = current_mode & 07777;
    const char *p = str;

    while (*p) {
        mode_t who = 0;
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            if (*p == 'u') who |= 0700;
            if (*p == 'g') who |= 0070;
            if (*p == 'o') who |= 0007;
            if (*p == 'a') who |= 0777;
            ++p;
        }
        if (who == 0) who = 0777;

        if (*p != '+' && *p != '-' && *p != '=') return 0;
        char op = *p++;

        mode_t perm_bits = 0;
        while (*p == 'r' || *p == 'w' || *p == 'x' || *p == 's' || *p == 't' || *p == 'X') {
            if (*p == 'r') perm_bits |= (S_IRUSR | S_IRGRP | S_IROTH);
            if (*p == 'w') perm_bits |= (S_IWUSR | S_IWGRP | S_IWOTH);
            if (*p == 'x') perm_bits |= (S_IXUSR | S_IXGRP | S_IXOTH);
            if (*p == 'X') perm_bits |= (S_IXUSR | S_IXGRP | S_IXOTH); /* Basic X support */
            if (*p == 's') perm_bits |= (S_ISUID | S_ISGID);
            if (*p == 't') perm_bits |= S_ISVTX;
            ++p;
        }

        mode_t mask = perm_bits & who;
        if (op == '+') {
            mode |= mask;
        } else if (op == '-') {
            mode &= ~mask;
        } else if (op == '=') {
            mode = (mode & ~who) | mask;
        }

        if (*p == ',') ++p;
    }

    if (out_mode) *out_mode = mode;
    return 1;
}

static int process_path(const char *path, mode_t target_mode) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (!opt_quiet) {
            fprintf(stderr, "chmod: cannot access '%s': %s\n", path, strerror(errno));
        }
        return 1;
    }

    /* chmod never changes permissions of a symbolic link itself */
    if (S_ISLNK(st.st_mode)) return 0;

    mode_t old_mode = st.st_mode & 07777;
    int changed = (old_mode != target_mode);

    if (chmod(path, target_mode) != 0) {
        if (!opt_quiet) {
            fprintf(stderr, "chmod: changing permissions of '%s': %s\n", path, strerror(errno));
        }
        return 1;
    }

    if (opt_verbose || (opt_changes && changed)) {
        if (changed) {
            printf("mode of '%s' changed from %04o to %04o\n", path, (unsigned int)old_mode, (unsigned int)target_mode);
        } else if (opt_verbose) {
            printf("mode of '%s' retained as %04o\n", path, (unsigned int)old_mode);
        }
    }

    int had_error = 0;
    if (S_ISDIR(st.st_mode) && opt_recursive) {
        DIR *dir = opendir(path);
        if (dir) {
            struct dirent *de;
            while ((de = readdir(dir)) != NULL) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                char subpath[1024];
                snprintf(subpath, sizeof(subpath), "%s/%s", path, de->d_name);
                if (process_path(subpath, target_mode) != 0) {
                    had_error = 1;
                }
            }
            closedir(dir);
        }
    }

    return had_error;
}

int main(int argc, char **argv) {
    static struct option long_options[] = {
        {"changes", no_argument, 0, 'c'},
        {"silent", no_argument, 0, 'f'},
        {"quiet", no_argument, 0, 'f'},
        {"verbose", no_argument, 0, 'v'},
        {"recursive", no_argument, 0, 'R'},
        {"reference", required_argument, 0, 200},
        {"help", no_argument, 0, 201},
        {"version", no_argument, 0, 202},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "cfvRh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c': opt_changes = 1; break;
            case 'f': opt_quiet = 1; break;
            case 'v': opt_verbose = 1; break;
            case 'R': opt_recursive = 1; break;
            case 'h': print_usage(); return 0;
            case 200: strncpy(ref_file, optarg, sizeof(ref_file) - 1); has_reference = 1; break;
            case 201: print_usage(); return 0;
            case 202: print_version(); return 0;
            default: return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "chmod: missing operand\nTry 'chmod --help' for more information.\n");
        return 1;
    }

    mode_t target_mode = 0;
    int mode_idx = optind;

    if (!has_reference) {
        if (optind + 1 >= argc) {
            fprintf(stderr, "chmod: missing operand after '%s'\nTry 'chmod --help' for more information.\n", argv[optind]);
            return 1;
        }
        
        /* Note: Simplification: assume mode is a single argument */
        const char *mode_str = argv[optind++];
        mode_idx = optind;
        
        /* For symbolic mode, we need an initial file to base on if needed, 
           or use 0 and apply relative to 0 which isn't standard.
           For now assume absolute or symbolic to be applied if known. */
        
        mode_t octal_val = 0;
        if (parse_octal_mode(mode_str, &octal_val)) {
            target_mode = octal_val;
        } else {
            /* Need a dummy for parsing if symbolic mode is relative */
            if (!parse_symbolic_mode(mode_str, 0755, &target_mode)) {
                fprintf(stderr, "chmod: invalid mode: '%s'\n", mode_str);
                return 1;
            }
        }
    } else {
        mode_idx = optind;
        struct stat ref_st;
        if (stat(ref_file, &ref_st) != 0) {
            fprintf(stderr, "chmod: failed to get attributes of '%s': %s\n", ref_file, strerror(errno));
            return 1;
        }
        target_mode = ref_st.st_mode & 07777;
    }

    int had_err = 0;
    for (int i = mode_idx; i < argc; ++i) {
        if (process_path(argv[i], target_mode) != 0) {
            had_err = 1;
        }
    }

    return had_err ? 1 : 0;
}
