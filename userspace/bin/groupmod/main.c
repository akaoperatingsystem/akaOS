#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GROUPMOD_VERSION "4.14.3 (akaOS coreutils)"

static int opt_non_unique = 0;
static gid_t opt_gid = (gid_t)-1;
static char *opt_new_name = NULL;
static char *opt_password = NULL;

static void print_usage(void) {
    printf("Usage: groupmod [options] GROUP\n"
           "Options:\n"
           "  -g, --gid GID           change group ID to GID\n"
           "  -h, --help              display this help message and exit\n"
           "  -n, --new-name NEW_NAME change group name to NEW_NAME\n"
           "  -o, --non-unique        allow non-unique GID\n"
           "  -p, --password PASSWORD change password to PASSWORD\n");
}

static int is_group_in_file(const char *groupname, const char *group_file) {
    FILE *f = fopen(group_file, "r");
    if (!f) return 0;
    char line[1024];
    size_t glen = strlen(groupname);
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, groupname, glen) == 0 && line[glen] == ':') {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

static int modify_group_in_file(const char *old_groupname, const char *new_groupname, gid_t gid, const char *password) {
    const char *group_file = getenv("AKAOS_GROUP_FILE");
    if (!group_file) group_file = "/etc/group";

    FILE *f = fopen(group_file, "r");
    if (!f) {
        fprintf(stderr, "groupmod: cannot open %s: %s\n", group_file, strerror(errno));
        return 10;
    }

    char temp_file[512];
    snprintf(temp_file, sizeof(temp_file), "%s.tmp.%d", group_file, getpid());
    FILE *tf = fopen(temp_file, "w");
    if (!tf) {
        fclose(f);
        fprintf(stderr, "groupmod: cannot create temporary file: %s\n", strerror(errno));
        return 10;
    }

    char line[1024];
    size_t glen = strlen(old_groupname);
    int modified = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, old_groupname, glen) == 0 && line[glen] == ':') {
            char *p1 = strchr(line, ':');
            const char *name = new_groupname ? new_groupname : old_groupname;
            const char *pwd = password ? password : "x"; 
            fprintf(tf, "%s:%s:%d:\n", name, pwd, (int)(gid == (gid_t)-1 ? atoi(p1 + 1) : gid));
            modified = 1;
        } else {
            fputs(line, tf);
        }
    }

    fclose(f);
    fclose(tf);

    if (!modified) {
        unlink(temp_file);
        return 4; 
    }

    if (rename(temp_file, group_file) != 0) {
        fprintf(stderr, "groupmod: cannot update %s: %s\n", group_file, strerror(errno));
        unlink(temp_file);
        return 10;
    }
    return 0;
}

int main(int argc, char **argv) {
    int c;
    static struct option long_options[] = {
        {"gid", required_argument, 0, 'g'},
        {"help", no_argument, 0, 'h'},
        {"new-name", required_argument, 0, 'n'},
        {"non-unique", no_argument, 0, 'o'},
        {"password", required_argument, 0, 'p'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "g:hn:op:", long_options, NULL)) != -1) {
        switch (c) {
            case 'g': opt_gid = (gid_t)atoi(optarg); break;
            case 'h': print_usage(); return 0;
            case 'n': opt_new_name = optarg; break;
            case 'o': opt_non_unique = 1; break;
            case 'p': opt_password = optarg; break;
            default: print_usage(); return 2;
        }
    }

    if (optind != argc - 1) {
        print_usage();
        return 2;
    }

    const char *old_groupname = argv[optind];
    const char *group_file = getenv("AKAOS_GROUP_FILE");
    if (!group_file) group_file = "/etc/group";

    if (!is_group_in_file(old_groupname, group_file)) {
        fprintf(stderr, "groupmod: group '%s' does not exist\n", old_groupname);
        return 4;
    }

    if (opt_new_name && is_group_in_file(opt_new_name, group_file)) {
        fprintf(stderr, "groupmod: group name '%s' already in use\n", opt_new_name);
        return 9;
    }

    return modify_group_in_file(old_groupname, opt_new_name, opt_gid, opt_password);
}
