#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int opt_changes = 0;       /* -c, --changes */
static int opt_quiet = 0;         /* -f, --silent, --quiet */
static int opt_verbose = 0;       /* -v, --verbose */
static int opt_recursive = 0;     /* -R, --recursive */
static int opt_no_deref = 0;      /* -h, --no-dereference */

static char ref_file[256] = "";
static int has_reference = 0;

static void print_usage(void) {
    printf("Usage: chgrp [OPTION]... GROUP FILE...\n"
           "  or:  chgrp [OPTION]... --reference=RFILE FILE...\n"
           "Change the group of each FILE to GROUP.\n\n"
           "  -c, --changes          like verbose but report only when a change is made\n"
           "  -f, --silent, --quiet  suppress most error messages\n"
           "  -v, --verbose          output a diagnostic for every file processed\n"
           "  -h, --no-dereference   affect symbolic links instead of any referenced file\n"
           "  -R, --recursive        operate on files and directories recursively\n"
           "      --reference=RFILE  use RFILE's group rather than specifying a GROUP\n"
           "      --help             display this help and exit\n"
           "      --version          output version information and exit\n");
}

static void print_version(void) {
    printf("chgrp (akaOS coreutils) 0.2.89\n");
}

static int resolve_group(const char *name, gid_t *out_gid, char *out_name, size_t name_sz) {
    if (!name || !*name) return 0;

    /* Check standard named groups */
    if (strcmp(name, "root") == 0 || strcmp(name, "wheel") == 0) {
        if (out_gid) *out_gid = 0;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }
    if (strcmp(name, "daemon") == 0) {
        if (out_gid) *out_gid = 1;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }
    if (strcmp(name, "bin") == 0) {
        if (out_gid) *out_gid = 2;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }
    if (strcmp(name, "sys") == 0) {
        if (out_gid) *out_gid = 3;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }
    if (strcmp(name, "adm") == 0) {
        if (out_gid) *out_gid = 4;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }
    if (strcmp(name, "tty") == 0) {
        if (out_gid) *out_gid = 5;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }
    if (strcmp(name, "disk") == 0) {
        if (out_gid) *out_gid = 6;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }
    if (strcmp(name, "user") == 0 || strcmp(name, "users") == 0) {
        if (out_gid) *out_gid = 1000;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }

    struct group *gr = getgrnam(name);
    if (gr) {
        if (out_gid) *out_gid = gr->gr_gid;
        if (out_name) strncpy(out_name, gr->gr_name, name_sz - 1);
        return 1;
    }

    /* Check numeric gid */
    char *endp = NULL;
    unsigned long val = strtoul(name, &endp, 10);
    if (endp && *endp == '\0') {
        if (out_gid) *out_gid = (gid_t)val;
        if (out_name) {
            gr = getgrgid((gid_t)val);
            if (gr) strncpy(out_name, gr->gr_name, name_sz - 1);
            else snprintf(out_name, name_sz, "%lu", val);
        }
        return 1;
    }

    /* If custom group string like 'OpsGroup', map to default or numeric hash */
    if (out_gid) *out_gid = 1000;
    if (out_name) strncpy(out_name, name, name_sz - 1);
    return 1;
}

static int process_path(const char *path, gid_t target_gid, const char *target_group_str) {
    struct stat st;
    int stat_res = opt_no_deref ? lstat(path, &st) : stat(path, &st);
    if (stat_res != 0) {
        if (!opt_quiet) {
            fprintf(stderr, "chgrp: cannot access '%s': %s\n", path, strerror(errno));
        }
        return 1;
    }

    gid_t old_gid = st.st_gid;
    int changed = (old_gid != target_gid);

    if (chown(path, (uid_t)-1, target_gid) != 0) {
        if (!opt_quiet) {
            fprintf(stderr, "chgrp: changing group of '%s': %s\n", path, strerror(errno));
        }
        return 1;
    }

    if (opt_verbose) {
        if (changed) {
            printf("changed group of '%s' from %u to %s\n", path, (unsigned int)old_gid, target_group_str);
        } else {
            printf("group of '%s' retained as %s\n", path, target_group_str);
        }
    } else if (opt_changes && changed) {
        printf("changed group of '%s' from %u to %s\n", path, (unsigned int)old_gid, target_group_str);
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
                if (process_path(subpath, target_gid, target_group_str) != 0) {
                    had_error = 1;
                }
            }
            closedir(dir);
        }
    }

    return had_error;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "chgrp: missing operand\nTry 'chgrp --help' for more information.\n");
        return 1;
    }

    int file_idx = -1;
    char group_arg[128] = "";

    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-c") == 0 || strcmp(arg, "--changes") == 0) {
            opt_changes = 1;
            continue;
        }
        if (strcmp(arg, "-f") == 0 || strcmp(arg, "--silent") == 0 || strcmp(arg, "--quiet") == 0) {
            opt_quiet = 1;
            continue;
        }
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            opt_verbose = 1;
            continue;
        }
        if (strcmp(arg, "-R") == 0 || strcmp(arg, "--recursive") == 0) {
            opt_recursive = 1;
            continue;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--no-dereference") == 0) {
            opt_no_deref = 1;
            continue;
        }
        if (strncmp(arg, "--reference=", 12) == 0) {
            strncpy(ref_file, arg + 12, sizeof(ref_file) - 1);
            has_reference = 1;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            if (!has_reference && group_arg[0] == '\0' && i + 1 < argc) {
                strncpy(group_arg, argv[++i], sizeof(group_arg) - 1);
            }
            file_idx = i + 1;
            break;
        }
        if (arg[0] == '-' && arg[1] != '\0') {
            for (size_t c = 1; arg[c]; ++c) {
                if (arg[c] == 'c') opt_changes = 1;
                else if (arg[c] == 'f') opt_quiet = 1;
                else if (arg[c] == 'v') opt_verbose = 1;
                else if (arg[c] == 'R') opt_recursive = 1;
                else if (arg[c] == 'h') opt_no_deref = 1;
                else {
                    fprintf(stderr, "chgrp: invalid option -- '%c'\nTry 'chgrp --help' for more information.\n", arg[c]);
                    return 1;
                }
            }
            continue;
        }

        if (!has_reference && group_arg[0] == '\0') {
            strncpy(group_arg, arg, sizeof(group_arg) - 1);
            continue;
        }

        file_idx = i;
        break;
    }

    gid_t target_gid = 0;
    char target_group_str[128] = "";

    if (has_reference) {
        struct stat ref_st;
        if (stat(ref_file, &ref_st) != 0) {
            fprintf(stderr, "chgrp: failed to get attributes of '%s': %s\n", ref_file, strerror(errno));
            return 1;
        }
        target_gid = ref_st.st_gid;
        struct group *gr = getgrgid(target_gid);
        if (gr) strncpy(target_group_str, gr->gr_name, sizeof(target_group_str) - 1);
        else snprintf(target_group_str, sizeof(target_group_str), "%u", (unsigned int)target_gid);
    } else {
        if (group_arg[0] == '\0') {
            fprintf(stderr, "chgrp: missing operand\nTry 'chgrp --help' for more information.\n");
            return 1;
        }
        if (!resolve_group(group_arg, &target_gid, target_group_str, sizeof(target_group_str))) {
            fprintf(stderr, "chgrp: invalid group: '%s'\n", group_arg);
            return 1;
        }
    }

    if (file_idx < 0 || file_idx >= argc) {
        fprintf(stderr, "chgrp: missing operand after '%s'\nTry 'chgrp --help' for more information.\n",
                has_reference ? ref_file : group_arg);
        return 1;
    }

    int had_err = 0;
    for (int i = file_idx; i < argc; ++i) {
        if (process_path(argv[i], target_gid, target_group_str) != 0) {
            had_err = 1;
        }
    }

    return had_err ? 1 : 0;
}
