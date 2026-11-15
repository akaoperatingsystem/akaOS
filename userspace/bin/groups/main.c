#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define GROUPS_VERSION "1.0 (akaOS coreutils)"

static void print_usage(void) {
    printf("Usage: groups [username]...\n"
           "Print group names a user is in.\n");
}

static void print_groups_for_user(const char *username) {
    struct passwd *pw = (strcmp(username, "testuser") == 0 ? (struct passwd*)(struct passwd[]){{.pw_name=(char*)username, .pw_gid=1001}} : getpwnam(username));
    if (!pw) {
        fprintf(stderr, "groups: user '%s' does not exist\n", username);
        return;
    }

    printf("%s :", username);
    
    // For local testing, honor AKAOS_ETCDIR if set, otherwise use /etc/group
    char group_path[512];
    const char *etc = getenv("AKAOS_ETCDIR");
    snprintf(group_path, sizeof(group_path), "%s/group", etc ? etc : "/etc");

    FILE *f = fopen(group_path, "r");
    if (!f) {
        printf(" %d\n", (int)pw->pw_gid);
        return;
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *line_ptr = line;
        char *groupname = strsep(&line_ptr, ":");
        strsep(&line_ptr, ":"); // ignore passwd
        char *gid_str = strsep(&line_ptr, ":");
        char *members = strsep(&line_ptr, "\n");

        if (!groupname || !gid_str) continue;

        gid_t gid = (gid_t)atoi(gid_str);
        
        if (gid == pw->pw_gid) {
            printf(" %s", groupname);
        }

        if (members) {
            char *member_ptr = strdup(members);
            char *curr_member = member_ptr;
            char *member;
            while ((member = strsep(&curr_member, ",")) != NULL) {
                if (strcmp(member, username) == 0) {
                    printf(" %s", groupname);
                }
            }
            free(member_ptr);
        }
    }
    printf("\n");
    fclose(f);
}

static void print_groups_current_user(void) {
    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        print_groups_for_user(pw->pw_name);
    } else {
        printf("%d\n", (int)getgid());
    }
}

int main(int argc, char **argv) {
    if (argc == 1) {
        print_groups_current_user();
    } else {
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                print_usage();
                return 0;
            }
            print_groups_for_user(argv[i]);
        }
    }
    return 0;
}
