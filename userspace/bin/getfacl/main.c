#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GETFACL_VERSION "2.3.1 (akaOS coreutils)"

static int opt_access = 0;       /* -a */
static int opt_default = 0;      /* -d */
static int opt_omit_header = 0;  /* -c */
static int opt_all_eff = 0;      /* -e */
static int opt_no_eff = 0;       /* -E */
static int opt_skip_base = 0;    /* -s */
static int opt_recursive = 0;    /* -R */
static int opt_logical = 0;      /* -L */
static int opt_physical = 0;     /* -P */
static int opt_tabular = 0;      /* -t */
static int opt_absolute = 0;     /* -p */
static int opt_numeric = 0;      /* -n */

static void print_version(void) {
    printf("getfacl %s\n", GETFACL_VERSION);
}

static void print_usage(void) {
    printf("Usage: getfacl [-aceEsRLPtpndvh] file ...\n"
           "       getfacl [-aceEsRLPtpndvh] -\n"
           "Get file access control lists.\n\n"
           "Options:\n"
           "  -a, --access           display the file access control list\n"
           "  -d, --default          display the default access control list\n"
           "  -c, --omit-header      do not display the comment header\n"
           "  -e, --all-effective    print all effective rights\n"
           "  -E, --no-effective     do not print effective rights\n"
           "  -s, --skip-base        skip files that only have the base ACL entries\n"
           "  -R, --recursive        recurse into subdirectories\n"
           "  -L, --logical          logical walk, follow symbolic links\n"
           "  -P, --physical         physical walk, do not follow symbolic links\n"
           "  -t, --tabular          use tabular output format\n"
           "  -p, --absolute-names   do not strip leading '/' on pathnames\n"
           "  -n, --numeric          print numeric user/group IDs\n"
           "  -v, --version          print version and exit\n"
           "  -h, --help             this help text\n");
}

static void rwx_to_str(mode_t mode, int shift, char out[4]) {
    out[0] = (mode & (0400 >> shift)) ? 'r' : '-';
    out[1] = (mode & (0200 >> shift)) ? 'w' : '-';
    out[2] = (mode & (0100 >> shift)) ? 'x' : '-';
    out[3] = '\0';
}

static int process_single_file(const char *path, int is_first);

static int walk_dir(const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) {
        fprintf(stderr, "getfacl: %s: %s\n", dir_path, strerror(errno));
        return 1;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        char subpath[512];
        if (strcmp(dir_path, "/") == 0) {
            snprintf(subpath, sizeof(subpath), "/%s", ent->d_name);
        } else {
            snprintf(subpath, sizeof(subpath), "%s/%s", dir_path, ent->d_name);
        }
        process_single_file(subpath, 0);
    }
    closedir(d);
    return 0;
}

static int process_single_file(const char *path, int is_first) {
    struct stat st;
    int stat_res = (opt_logical) ? stat(path, &st) : lstat(path, &st);
    if (stat_res != 0) {
        fprintf(stderr, "getfacl: %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (opt_skip_base) {
        /* Since akaOS filesystems currently do not store extended ACLs,
         * all files only have base entries. With -s, skip all base-only files. */
        if (opt_recursive && S_ISDIR(st.st_mode)) {
            walk_dir(path);
        }
        return 0;
    }

    if (!is_first) {
        printf("\n");
    }

    const char *display_path = path;
    if (!opt_absolute && display_path[0] == '/' && display_path[1] != '\0') {
        while (*display_path == '/') display_path++;
    }

    char u_rwx[4], g_rwx[4], o_rwx[4];
    rwx_to_str(st.st_mode, 0, u_rwx);
    rwx_to_str(st.st_mode, 3, g_rwx);
    rwx_to_str(st.st_mode, 6, o_rwx);

    /* Header */
    if (!opt_omit_header) {
        printf("# file: %s\n", display_path);
        if (opt_numeric) {
            printf("# owner: %u\n", (unsigned int)st.st_uid);
            printf("# group: %u\n", (unsigned int)st.st_gid);
        } else {
            struct passwd *pw = getpwuid(st.st_uid);
            struct group *gr = getgrgid(st.st_gid);
            printf("# owner: %s\n", pw ? pw->pw_name : "root");
            printf("# group: %s\n", gr ? gr->gr_name : "root");
        }

        /* Flags comment (setuid, setgid, sticky) if set */
        int has_flags = (st.st_mode & (S_ISUID | S_ISGID | S_ISVTX));
        if (has_flags) {
            char flags[4];
            flags[0] = (st.st_mode & S_ISUID) ? 's' : '-';
            flags[1] = (st.st_mode & S_ISGID) ? 's' : '-';
            flags[2] = (st.st_mode & S_ISVTX) ? 't' : '-';
            flags[3] = '\0';
            printf("# flags: %s\n", flags);
        }
    }

    /* Tabular mode: side-by-side ACL and default ACL */
    if (opt_tabular) {
        printf("%-12s %-12s\n", "# file: ", display_path);
        printf("USER   %-6s  %-6s\n", u_rwx, S_ISDIR(st.st_mode) ? u_rwx : "");
        printf("GROUP  %-6s  %-6s\n", g_rwx, S_ISDIR(st.st_mode) ? g_rwx : "");
        printf("OTHER  %-6s  %-6s\n", o_rwx, S_ISDIR(st.st_mode) ? o_rwx : "");
    } else {
        /* Standard access ACL entries */
        if (!opt_default || opt_access) {
            printf("user::%s\n", u_rwx);
            printf("group::%s\n", g_rwx);
            printf("other::%s\n", o_rwx);
        }

        /* Default ACL entries for directories */
        if (S_ISDIR(st.st_mode) && (opt_default || (!opt_access && !getenv("POSIXLY_CORRECT")))) {
            printf("default:user::%s\n", u_rwx);
            printf("default:group::%s\n", g_rwx);
            printf("default:other::%s\n", o_rwx);
        }
    }

    if (opt_recursive && S_ISDIR(st.st_mode)) {
        walk_dir(path);
    }

    return 0;
}

int main(int argc, char **argv) {
    char *files[256];
    int num_files = 0;
    int read_stdin = 0;

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "--") == 0) {
            for (int j = i + 1; j < argc && num_files < 256; ++j) {
                files[num_files++] = argv[j];
            }
            break;
        }
        if (strcmp(arg, "-") == 0) {
            read_stdin = 1;
            continue;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "--access") == 0) { opt_access = 1; continue; }
            if (strcmp(arg, "--default") == 0) { opt_default = 1; continue; }
            if (strcmp(arg, "--omit-header") == 0) { opt_omit_header = 1; continue; }
            if (strcmp(arg, "--all-effective") == 0) { opt_all_eff = 1; continue; }
            if (strcmp(arg, "--no-effective") == 0) { opt_no_eff = 1; continue; }
            if (strcmp(arg, "--skip-base") == 0) { opt_skip_base = 1; continue; }
            if (strcmp(arg, "--recursive") == 0) { opt_recursive = 1; continue; }
            if (strcmp(arg, "--logical") == 0) { opt_logical = 1; opt_physical = 0; continue; }
            if (strcmp(arg, "--physical") == 0) { opt_physical = 1; opt_logical = 0; continue; }
            if (strcmp(arg, "--tabular") == 0) { opt_tabular = 1; continue; }
            if (strcmp(arg, "--absolute-names") == 0) { opt_absolute = 1; continue; }
            if (strcmp(arg, "--numeric") == 0) { opt_numeric = 1; continue; }

            for (size_t c = 1; arg[c]; ++c) {
                switch (arg[c]) {
                    case 'a': opt_access = 1; break;
                    case 'd': opt_default = 1; break;
                    case 'c': opt_omit_header = 1; break;
                    case 'e': opt_all_eff = 1; break;
                    case 'E': opt_no_eff = 1; break;
                    case 's': opt_skip_base = 1; break;
                    case 'R': opt_recursive = 1; break;
                    case 'L': opt_logical = 1; opt_physical = 0; break;
                    case 'P': opt_physical = 1; opt_logical = 0; break;
                    case 't': opt_tabular = 1; break;
                    case 'p': opt_absolute = 1; break;
                    case 'n': opt_numeric = 1; break;
                    default:
                        fprintf(stderr, "getfacl: invalid option -- '%c'\n", arg[c]);
                        print_usage();
                        return 1;
                }
            }
            continue;
        }

        if (num_files < 256) {
            files[num_files++] = arg;
        }
    }

    if (num_files == 0 && !read_stdin) {
        if (getenv("POSIXLY_CORRECT")) {
            read_stdin = 1;
        } else {
            print_usage();
            return 1;
        }
    }

    int ret = 0;
    int is_first = 1;

    if (read_stdin) {
        char line[512];
        while (fgets(line, sizeof(line), stdin)) {
            line[strcspn(line, "\r\n")] = '\0';
            if (line[0] == '\0') continue;
            if (process_single_file(line, is_first) != 0) {
                ret = 1;
            }
            is_first = 0;
        }
    }

    for (int i = 0; i < num_files; ++i) {
        if (process_single_file(files[i], is_first) != 0) {
            ret = 1;
        }
        is_first = 0;
    }

    return ret;
}
