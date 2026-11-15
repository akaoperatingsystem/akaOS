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

#define GROUPADD_VERSION "4.14.3 (akaOS coreutils)"

static int opt_force = 0;
static int opt_non_unique = 0;
static int opt_system = 0;
static gid_t opt_gid = (gid_t)-1;
static char *opt_password = NULL;
static char *opt_key = NULL;

static void print_usage(void) {
    printf("Usage: groupadd [options] GROUP\n"
           "Options:\n"
           "  -f, --force               exit success if group already exists\n"
           "  -g, --gid GID             use GID for the new group\n"
           "  -h, --help                display this help message and exit\n"
           "  -K, --key KEY=VALUE       override /etc/login.defs defaults\n"
           "  -o, --non-unique          allow non-unique GID\n"
           "  -p, --password PASSWORD   use encrypted password for the new group\n"
           "  -r, --system              create a system group\n");
}

static int is_group_exists(const char *groupname) {
    return getgrnam(groupname) != NULL;
}

static int is_gid_exists(gid_t gid) {
    return getgrgid(gid) != NULL;
}

static int add_group_to_file(const char *groupname, gid_t gid, const char *password) {
    const char *group_file = getenv("AKAOS_GROUP_FILE");
    if (!group_file) group_file = "/etc/group";
    FILE *f = fopen(group_file, "a");
    if (!f) {
        fprintf(stderr, "groupadd: cannot open /etc/group: %s\n", strerror(errno));
        return 10;
    }
    fprintf(f, "%s:%s:%d:\n", groupname, password ? password : "x", (int)gid);
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    int c;
    static struct option long_options[] = {
        {"force", no_argument, 0, 'f'},
        {"gid", required_argument, 0, 'g'},
        {"help", no_argument, 0, 'h'},
        {"key", required_argument, 0, 'K'},
        {"non-unique", no_argument, 0, 'o'},
        {"password", required_argument, 0, 'p'},
        {"system", no_argument, 0, 'r'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "fg:hK:op:r", long_options, NULL)) != -1) {
        switch (c) {
            case 'f': opt_force = 1; break;
            case 'g': opt_gid = (gid_t)atoi(optarg); break;
            case 'h': print_usage(); return 0;
            case 'K': opt_key = optarg; break;
            case 'o': opt_non_unique = 1; break;
            case 'p': opt_password = optarg; break;
            case 'r': opt_system = 1; break;
            default: print_usage(); return 2;
        }
    }

    if (optind != argc - 1) {
        print_usage();
        return 2;
    }

    const char *groupname = argv[optind];
    if (is_group_exists(groupname)) {
        if (opt_force) return 0;
        fprintf(stderr, "groupadd: group '%s' already exists\n", groupname);
        return 9;
    }

    /* Simple GID allocation logic */
    gid_t gid = opt_gid;
    if (gid == (gid_t)-1) {
        /* Default: find max GID and add 1 */
        /* For now, just a placeholder as akaOS doesn't have a complex group management library */
        gid = 1000;
        while (is_gid_exists(gid)) gid++;
    } else {
        if (!opt_non_unique && is_gid_exists(gid)) {
            fprintf(stderr, "groupadd: GID '%d' already exists\n", (int)gid);
            return 4;
        }
    }

    return add_group_to_file(groupname, gid, opt_password);
}
