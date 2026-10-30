#include <akaos/account.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define SUDOERS_FILE "/etc/sudoers"
#define GROUP_FILE   "/etc/group"
#define BUFFER_SIZE  4096

/* Keep large file-sized buffers in process BSS to avoid small user stack overflow */
static char group_buf[BUFFER_SIZE];
static char sudoers_buf[BUFFER_SIZE];
static char group_members[512];
static char *cmd_argv_buf[64];

/* Checks if user is in group in /etc/group or has user_gid matching group */
static int check_group_membership(const char *group_name, const char *user_name, gid_t user_gid) {
    int fd = open(GROUP_FILE, O_RDONLY);
    if (fd < 0) return 0;

    ssize_t n = read(fd, group_buf, sizeof(group_buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    group_buf[n] = '\0';

    const char *line = group_buf;
    while (*line) {
        const char *next_line = strchr(line, '\n');
        size_t line_len = next_line ? (size_t)(next_line - line) : strlen(line);

        char gname[32] = {0};
        unsigned gid_val = 0;

        size_t colons[3];
        int colon_count = 0;
        for (size_t i = 0; i < line_len && colon_count < 3; ++i) {
            if (line[i] == ':') {
                colons[colon_count++] = i;
            }
        }

        if (colon_count >= 3) {
            size_t gn_len = colons[0] < sizeof(gname) - 1 ? colons[0] : sizeof(gname) - 1;
            memcpy(gname, line, gn_len);
            gname[gn_len] = '\0';

            const char *gp = line + colons[1] + 1;
            while (*gp >= '0' && *gp <= '9' && gp < line + colons[2]) {
                gid_val = gid_val * 10 + (unsigned)(*gp++ - '0');
            }

            size_t mem_start = colons[2] + 1;
            if (mem_start < line_len) {
                size_t mem_len = line_len - mem_start;
                if (mem_len >= sizeof(group_members)) mem_len = sizeof(group_members) - 1;
                memcpy(group_members, line + mem_start, mem_len);
                group_members[mem_len] = '\0';
            } else {
                group_members[0] = '\0';
            }

            if (strcmp(gname, group_name) == 0) {
                if (user_gid == (gid_t)gid_val) return 1;
                char *token = group_members;
                while (*token) {
                    char *comma = strchr(token, ',');
                    if (comma) *comma = '\0';
                    while (*token == ' ' || *token == '\t') token++;
                    char *end = token + strlen(token) - 1;
                    while (end > token && (*end == ' ' || *end == '\t' || *end == '\r')) *end-- = '\0';
                    if (strcmp(token, user_name) == 0) return 1;
                    if (!comma) break;
                    token = comma + 1;
                }
            }
        }

        if (!next_line) break;
        line = next_line + 1;
    }

    return 0;
}

/* Validates caller in /etc/sudoers */
static int check_sudoers(const char *caller_name, gid_t caller_gid,
                         const char *target_name, const char *command,
                         int *out_nopasswd) {
    if (out_nopasswd) *out_nopasswd = 0;

    int fd = open(SUDOERS_FILE, O_RDONLY);
    if (fd < 0) return 0;

    ssize_t n = read(fd, sudoers_buf, sizeof(sudoers_buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    sudoers_buf[n] = '\0';

    const char *line = sudoers_buf;
    while (*line) {
        const char *next_line = strchr(line, '\n');
        size_t line_len = next_line ? (size_t)(next_line - line) : strlen(line);

        char lbuf[256];
        if (line_len >= sizeof(lbuf)) line_len = sizeof(lbuf) - 1;
        memcpy(lbuf, line, line_len);
        lbuf[line_len] = '\0';

        char *hash = strchr(lbuf, '#');
        if (hash) *hash = '\0';

        char *p = lbuf;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\0') {
            char *user_spec = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';

            int user_match = 0;
            if (user_spec[0] == '%') {
                if (check_group_membership(user_spec + 1, caller_name, caller_gid)) {
                    user_match = 1;
                }
            } else if (strcmp(user_spec, "ALL") == 0) {
                user_match = 1;
            } else if (strcmp(user_spec, caller_name) == 0) {
                user_match = 1;
            }

            if (user_match) {
                while (*p == ' ' || *p == '\t') p++;
                char *eq = strchr(p, '=');
                if (eq) {
                    char *paren_open = strchr(eq, '(');
                    char *paren_close = paren_open ? strchr(paren_open, ')') : NULL;
                    int target_match = 1;
                    if (paren_open && paren_close) {
                        *paren_close = '\0';
                        char *target_spec = paren_open + 1;
                        char *colon = strchr(target_spec, ':');
                        if (colon) *colon = '\0';
                        if (strcmp(target_spec, "ALL") != 0 &&
                            strcmp(target_spec, target_name) != 0) {
                            target_match = 0;
                        }
                        p = paren_close + 1;
                    } else {
                        p = eq + 1;
                    }

                    if (target_match) {
                        char *nopw = strstr(p, "NOPASSWD:");
                        if (nopw) {
                            if (out_nopasswd) *out_nopasswd = 1;
                            p = nopw + 9;
                        }

                        while (*p == ' ' || *p == '\t') p++;
                        char *end = p + strlen(p) - 1;
                        while (end > p && (*end == ' ' || *end == '\t' || *end == '\r')) *end-- = '\0';

                        if (strcmp(p, "ALL") == 0) {
                            return 1;
                        }
                        if (command && (strcmp(p, command) == 0 || strstr(command, p) != NULL)) {
                            return 1;
                        }
                    }
                }
            }
        }

        if (!next_line) break;
        line = next_line + 1;
    }

    return 0;
}

static void print_usage(void) {
    puts("Usage: sudo [-h] [-u USER] [-i|-s] [COMMAND [ARG...]]\n"
         "Execute a command as another user (default: root).\n\n"
         "Options:\n"
         "  -h, --help    display this help message and exit\n"
         "  -u USER       run command as specified user\n"
         "  -i, -s        run an interactive shell as the target user");
}

int main(int argc, char **argv) {
    const char *target_user = "root";
    int shell_mode = 0;
    int cmd_idx = 1;

    while (cmd_idx < argc) {
        if (strcmp(argv[cmd_idx], "-h") == 0 || strcmp(argv[cmd_idx], "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(argv[cmd_idx], "-u") == 0) {
            if (cmd_idx + 1 >= argc) {
                puts("sudo: option requires an argument -- 'u'");
                return 1;
            }
            target_user = argv[cmd_idx + 1];
            cmd_idx += 2;
            continue;
        }
        if (strcmp(argv[cmd_idx], "-i") == 0 || strcmp(argv[cmd_idx], "-s") == 0) {
            shell_mode = 1;
            cmd_idx++;
            continue;
        }
        if (strcmp(argv[cmd_idx], "--") == 0) {
            cmd_idx++;
            break;
        }
        if (argv[cmd_idx][0] == '-') {
            printf("sudo: invalid option -- '%s'\n", argv[cmd_idx]);
            print_usage();
            return 1;
        }
        break;
    }

    if (!shell_mode && cmd_idx >= argc) {
        print_usage();
        return 1;
    }

    uid_t real_uid = getuid();
    struct akaos_account caller;
    if (!akaos_account_by_uid(real_uid, &caller)) {
        puts("sudo: current account is unknown");
        return 1;
    }

    struct akaos_account target;
    if (!akaos_account_by_name(target_user, &target)) {
        printf("sudo: unknown user: %s\n", target_user);
        return 1;
    }

    int nopasswd = 0;
    int authorized = (real_uid == 0) || check_sudoers(caller.name, caller.gid, target.name,
                                                      shell_mode ? target.shell : argv[cmd_idx],
                                                      &nopasswd);
    if (!authorized) {
        printf("sudo: %s is not in the sudoers file. This incident will be reported.\n", caller.name);
        return 1;
    }

    if (real_uid != 0 && !nopasswd) {
        char prompt[128];
        snprintf(prompt, sizeof(prompt), "[sudo] password for %s: ", caller.name);
        int success = 0;
        for (int attempt = 0; attempt < 3; ++attempt) {
            char password[AKAOS_PASSWORD_MAX + 1];
            if (!akaos_read_secret(prompt, password, sizeof(password))) {
                puts("\nsudo: authentication aborted");
                return 1;
            }
            if (akaos_account_verify(&caller, password)) {
                memset(password, 0, sizeof(password));
                success = 1;
                break;
            }
            memset(password, 0, sizeof(password));
            if (attempt < 2) {
                puts("Sorry, try again.");
            } else {
                puts("sudo: 3 incorrect password attempts");
            }
        }
        if (!success) {
            return 1;
        }
    }

    if (setgid(target.gid) != 0 || setuid(target.uid) != 0) {
        puts("sudo: unable to establish credentials");
        return 1;
    }

    if (shell_mode) {
        if (chdir(target.home) != 0) chdir("/");
        char *shell_argv[] = {target.shell, NULL};
        execv(target.shell, shell_argv);
        printf("sudo: unable to execute %s\n", target.shell);
        return 1;
    }

    const char *cmd = argv[cmd_idx];
    char resolved_path[128];
    const char *exec_path = cmd;

    if (cmd[0] != '/' && cmd[0] != '.') {
        snprintf(resolved_path, sizeof(resolved_path), "/bin/%s", cmd);
        struct akaos_stat st;
        if (stat(resolved_path, &st) == 0) {
            exec_path = resolved_path;
        }
    }

    int arg_count = 0;
    cmd_argv_buf[arg_count++] = (char *)cmd;
    for (int i = cmd_idx + 1; i < argc && arg_count < 63; ++i) {
        cmd_argv_buf[arg_count++] = argv[i];
    }
    cmd_argv_buf[arg_count] = NULL;

    execv(exec_path, cmd_argv_buf);
    printf("sudo: %s: command not found\n", cmd);
    return 1;
}
