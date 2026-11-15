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

#define GROUPDEL_VERSION "4.14.3 (akaOS coreutils)"

static void print_usage(void) {
    printf("Usage: groupdel [options] GROUP\n"
           "Delete a user security group.\n\n"
           "Options:\n"
           "  -h, --help     display this help message and exit\n"
           "  -R, --root CHROOT_DIR  directory to chroot into\n");
}

/* Check if the group is the primary group for any user */
static int is_primary_group(gid_t gid) {
    struct passwd *pw;
    setpwent();
    while ((pw = getpwent()) != NULL) {
        if (pw->pw_gid == gid) {
            endpwent();
            return 1;
        }
    }
    endpwent();
    return 0;
}

static int remove_group_from_file(const char *groupname) {
    const char *group_file = getenv("AKAOS_GROUP_FILE");
    if (!group_file) group_file = "/etc/group";

    FILE *f = fopen(group_file, "r");
    if (!f) {
        fprintf(stderr, "groupdel: cannot open %s: %s\n", group_file, strerror(errno));
        return 10;
    }

    char temp_file[512];
    snprintf(temp_file, sizeof(temp_file), "%s.tmp.%d", group_file, getpid());
    FILE *tf = fopen(temp_file, "w");
    if (!tf) {
        fclose(f);
        fprintf(stderr, "groupdel: cannot create temporary file: %s\n", strerror(errno));
        return 10;
    }

    char line[1024];
    size_t glen = strlen(groupname);
    int deleted = 0;

    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, groupname, glen) == 0 && line[glen] == ':') {
            deleted = 1;
            continue; // Skip this line to delete the group
        }
        fputs(line, tf);
    }

    fclose(f);
    fclose(tf);

    if (!deleted) {
        unlink(temp_file);
        return 6; // Group doesn't exist in file
    }

    if (rename(temp_file, group_file) != 0) {
        fprintf(stderr, "groupdel: cannot update %s: %s\n", group_file, strerror(errno));
        unlink(temp_file);
        return 10;
    }

    return 0;
}

int main(int argc, char **argv) {
    int c;
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"root", required_argument, 0, 'R'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "hR:", long_options, NULL)) != -1) {
        switch (c) {
            case 'h': print_usage(); return 0;
            case 'R': /* Support for chroot directory if needed */ break;
            default: print_usage(); return 2;
        }
    }

    if (optind != argc - 1) {
        print_usage();
        return 2;
    }

    const char *groupname = argv[optind];
    struct group *grp = getgrnam(groupname);
    if (!grp) {
        fprintf(stderr, "groupdel: group '%s' does not exist\n", groupname);
        return 6;
    }

    if (is_primary_group(grp->gr_gid)) {
        fprintf(stderr, "groupdel: cannot remove the primary group of an existing user\n");
        return 8;
    }

    return remove_group_from_file(groupname);
}
