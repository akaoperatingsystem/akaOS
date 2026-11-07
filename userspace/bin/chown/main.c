#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
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
static int opt_dereference = 1;   /* default 1, --dereference vs -h/--no-dereference */

static char ref_file[256] = "";
static int has_reference = 0;

static void print_usage(void) {
    printf("Usage: chown [OPTION]... [OWNER][:[GROUP]] FILE...\n"
           "  or:  chown [OPTION]... --reference=RFILE FILE...\n"
           "Change the owner and/or group of each FILE to OWNER and/or GROUP.\n\n"
           "  -c, --changes          like verbose but report only when a change is made\n"
           "  -f, --silent, --quiet  suppress most error messages\n"
           "  -v, --verbose          output a diagnostic for every file processed\n"
           "      --dereference      affect the referent of each symbolic link (default)\n"
           "  -h, --no-dereference   affect symbolic links instead of any referenced file\n"
           "  -R, --recursive        operate on files and directories recursively\n"
           "      --reference=RFILE  use RFILE's owner and group rather than specifying values\n"
           "      --help             display this help and exit\n"
           "      --version          output version information and exit\n\n"
           "Owner is unchanged if missing. Group is unchanged if missing, but changed to\n"
           "login group if implied by ':' following a symbolic OWNER.\n");
}

static void print_version(void) {
    printf("chown (akaOS coreutils) 0.2.94\n");
}

static int parse_user(const char *name, uid_t *out_uid, gid_t *default_gid, char *out_name, size_t name_sz) {
    if (!name || !*name) return 0;

    if (strcmp(name, "root") == 0) {
        if (out_uid) *out_uid = 0;
        if (default_gid) *default_gid = 0;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }
    if (strcmp(name, "daemon") == 0) {
        if (out_uid) *out_uid = 1;
        if (default_gid) *default_gid = 1;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }
    if (strcmp(name, "bin") == 0) {
        if (out_uid) *out_uid = 2;
        if (default_gid) *default_gid = 2;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }
    if (strcmp(name, "user") == 0) {
        if (out_uid) *out_uid = 1000;
        if (default_gid) *default_gid = 1000;
        if (out_name) strncpy(out_name, name, name_sz - 1);
        return 1;
    }

    struct passwd *pw = getpwnam(name);
    if (pw) {
        if (out_uid) *out_uid = pw->pw_uid;
        if (default_gid) *default_gid = pw->pw_gid;
        if (out_name) strncpy(out_name, pw->pw_name, name_sz - 1);
        return 1;
    }

    char *endp = NULL;
    unsigned long val = strtoul(name, &endp, 10);
    if (endp && *endp == '\0') {
        if (out_uid) *out_uid = (uid_t)val;
        pw = getpwuid((uid_t)val);
        if (pw) {
            if (default_gid) *default_gid = pw->pw_gid;
            if (out_name) strncpy(out_name, pw->pw_name, name_sz - 1);
        } else {
            if (default_gid) *default_gid = (gid_t)val;
            if (out_name) snprintf(out_name, name_sz, "%lu", val);
        }
        return 1;
    }

    /* Fallback for unrecognized username: assume default user id */
    if (out_uid) *out_uid = 1000;
    if (default_gid) *default_gid = 1000;
    if (out_name) strncpy(out_name, name, name_sz - 1);
    return 1;
}

static int parse_group(const char *name, gid_t *out_gid, char *out_name, size_t name_sz) {
    if (!name || !*name) return 0;

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
    if (strcmp(name, "user") == 0 || strcmp(name, "users") == 0 || strcmp(name, "staff") == 0) {
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

    char *endp = NULL;
    unsigned long val = strtoul(name, &endp, 10);
    if (endp && *endp == '\0') {
        if (out_gid) *out_gid = (gid_t)val;
        gr = getgrgid((gid_t)val);
        if (gr) strncpy(out_name, gr->gr_name, name_sz - 1);
        else snprintf(out_name, name_sz, "%lu", val);
        return 1;
    }

    if (out_gid) *out_gid = 1000;
    if (out_name) strncpy(out_name, name, name_sz - 1);
    return 1;
}

static int process_path(const char *path, uid_t target_uid, gid_t target_gid,
                         int have_target_uid, int have_target_gid,
                         const char *target_u_str, const char *target_g_str) {
    struct stat st;
    int stat_res = opt_dereference ? stat(path, &st) : lstat(path, &st);
    if (stat_res != 0) {
        if (!opt_quiet) {
            fprintf(stderr, "chown: cannot access '%s': %s\n", path, strerror(errno));
        }
        return 1;
    }

    uid_t old_uid = st.st_uid;
    gid_t old_gid = st.st_gid;

    uid_t new_uid = have_target_uid ? target_uid : old_uid;
    gid_t new_gid = have_target_gid ? target_gid : old_gid;

    int changed = (old_uid != new_uid) || (old_gid != new_gid);

    if (chown(path, new_uid, new_gid) != 0) {
        if (!opt_quiet) {
            fprintf(stderr, "chown: changing ownership of '%s': %s\n", path, strerror(errno));
        }
        return 1;
    }

    if (opt_verbose) {
        if (changed) {
            printf("changed ownership of '%s' from %u:%u to %s:%s\n",
                   path, (unsigned int)old_uid, (unsigned int)old_gid,
                   have_target_uid ? target_u_str : "",
                   have_target_gid ? target_g_str : "");
        } else {
            printf("ownership of '%s' retained as %s:%s\n",
                   path,
                   have_target_uid ? target_u_str : "",
                   have_target_gid ? target_g_str : "");
        }
    } else if (opt_changes && changed) {
        printf("changed ownership of '%s' from %u:%u to %s:%s\n",
               path, (unsigned int)old_uid, (unsigned int)old_gid,
               have_target_uid ? target_u_str : "",
               have_target_gid ? target_g_str : "");
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
                if (process_path(subpath, target_uid, target_gid,
                                 have_target_uid, have_target_gid,
                                 target_u_str, target_g_str) != 0) {
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
        fprintf(stderr, "chown: missing operand\nTry 'chown --help' for more information.\n");
        return 1;
    }

    int file_idx = -1;
    char spec_arg[128] = "";

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
        if (strcmp(arg, "--dereference") == 0) {
            opt_dereference = 1;
            continue;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--no-dereference") == 0) {
            opt_dereference = 0;
            continue;
        }
        if (strncmp(arg, "--reference=", 12) == 0) {
            strncpy(ref_file, arg + 12, sizeof(ref_file) - 1);
            has_reference = 1;
            continue;
        }
        if (strcmp(arg, "--") == 0) {
            if (!has_reference && spec_arg[0] == '\0' && i + 1 < argc) {
                strncpy(spec_arg, argv[++i], sizeof(spec_arg) - 1);
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
                else if (arg[c] == 'h') opt_dereference = 0;
                else {
                    fprintf(stderr, "chown: invalid option -- '%c'\nTry 'chown --help' for more information.\n", arg[c]);
                    return 1;
                }
            }
            continue;
        }

        if (!has_reference && spec_arg[0] == '\0') {
            strncpy(spec_arg, arg, sizeof(spec_arg) - 1);
            continue;
        }

        file_idx = i;
        break;
    }

    uid_t target_uid = (uid_t)-1;
    gid_t target_gid = (gid_t)-1;
    int have_target_uid = 0;
    int have_target_gid = 0;
    char target_u_str[64] = "";
    char target_g_str[64] = "";

    if (has_reference) {
        struct stat ref_st;
        if (stat(ref_file, &ref_st) != 0) {
            fprintf(stderr, "chown: failed to get attributes of '%s': %s\n", ref_file, strerror(errno));
            return 1;
        }
        target_uid = ref_st.st_uid;
        target_gid = ref_st.st_gid;
        have_target_uid = 1;
        have_target_gid = 1;

        struct passwd *pw = getpwuid(target_uid);
        if (pw) strncpy(target_u_str, pw->pw_name, sizeof(target_u_str) - 1);
        else snprintf(target_u_str, sizeof(target_u_str), "%u", (unsigned int)target_uid);

        struct group *gr = getgrgid(target_gid);
        if (gr) strncpy(target_g_str, gr->gr_name, sizeof(target_g_str) - 1);
        else snprintf(target_g_str, sizeof(target_g_str), "%u", (unsigned int)target_gid);
    } else {
        if (spec_arg[0] == '\0') {
            fprintf(stderr, "chown: missing operand\nTry 'chown --help' for more information.\n");
            return 1;
        }

        char user_buf[64] = "";
        char group_buf[64] = "";
        int has_delim = 0;
        int empty_after_delim = 0;

        char *delim = strchr(spec_arg, ':');
        if (!delim) delim = strchr(spec_arg, '.');

        if (delim) {
            has_delim = 1;
            size_t ulen = (size_t)(delim - spec_arg);
            if (ulen >= sizeof(user_buf)) ulen = sizeof(user_buf) - 1;
            memcpy(user_buf, spec_arg, ulen);
            user_buf[ulen] = '\0';

            const char *gstr = delim + 1;
            if (*gstr == '\0') {
                empty_after_delim = 1;
            } else {
                strncpy(group_buf, gstr, sizeof(group_buf) - 1);
            }
        } else {
            strncpy(user_buf, spec_arg, sizeof(user_buf) - 1);
        }

        gid_t default_gid = 0;
        if (user_buf[0] != '\0') {
            if (!parse_user(user_buf, &target_uid, &default_gid, target_u_str, sizeof(target_u_str))) {
                fprintf(stderr, "chown: invalid user: '%s'\n", user_buf);
                return 1;
            }
            have_target_uid = 1;
        }

        if (has_delim && empty_after_delim && have_target_uid) {
            /* USER: or USER. -> set group to user's login group */
            target_gid = default_gid;
            have_target_gid = 1;
            struct group *gr = getgrgid(target_gid);
            if (gr) strncpy(target_g_str, gr->gr_name, sizeof(target_g_str) - 1);
            else snprintf(target_g_str, sizeof(target_g_str), "%u", (unsigned int)target_gid);
        } else if (group_buf[0] != '\0') {
            if (!parse_group(group_buf, &target_gid, target_g_str, sizeof(target_g_str))) {
                fprintf(stderr, "chown: invalid group: '%s'\n", group_buf);
                return 1;
            }
            have_target_gid = 1;
        }
    }

    if (file_idx < 0 || file_idx >= argc) {
        fprintf(stderr, "chown: missing operand after '%s'\nTry 'chown --help' for more information.\n",
                has_reference ? ref_file : spec_arg);
        return 1;
    }

    int had_err = 0;
    for (int i = file_idx; i < argc; ++i) {
        if (process_path(argv[i], target_uid, target_gid,
                         have_target_uid, have_target_gid,
                         target_u_str, target_g_str) != 0) {
            had_err = 1;
        }
    }

    return had_err ? 1 : 0;
}
