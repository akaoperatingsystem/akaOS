#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>

#define SPOOL_CRONTABS "/var/spool/cron/crontabs"

static const char *get_username(const char *user) {
    if (user) return user;
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_name : "root";
}

static void get_crontab_path(char *buf, size_t size, const char *user) {
    snprintf(buf, size, "%s/%s", SPOOL_CRONTABS, get_username(user));
}

int main(int argc, char **argv) {
    const char *user = NULL;
    
    
    /* Simple argument parsing */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-u") == 0 && i + 1 < argc) {
            user = argv[++i];
        } else if (strcmp(argv[i], "-l") == 0) {
            char path[512];
            get_crontab_path(path, sizeof(path), user);
            char cmd[600];
            snprintf(cmd, sizeof(cmd), "cat %s", path);
            return system(cmd);
        } else if (strcmp(argv[i], "-r") == 0) {
            char path[512];
            get_crontab_path(path, sizeof(path), user);
            return unlink(path);
        } else if (strcmp(argv[i], "-e") == 0) {
            char path[512];
            get_crontab_path(path, sizeof(path), user);
            const char *editor = getenv("VISUAL");
            if (!editor) editor = getenv("EDITOR");
            if (!editor) editor = "vi";
            char cmd[600];
            snprintf(cmd, sizeof(cmd), "%s %s", editor, path);
            return system(cmd);
        } else {
            /* Treat as file */
            char path[512];
            get_crontab_path(path, sizeof(path), user);
            FILE *in = (strcmp(argv[i], "-") == 0) ? stdin : fopen(argv[i], "r");
            if (!in) { perror(argv[i]); return 1; }
            FILE *out = fopen(path, "w");
            if (!out) { perror(path); if (in != stdin) fclose(in); return 1; }
            char buf[1024];
            while (fgets(buf, sizeof(buf), in)) fputs(buf, out);
            fclose(out);
            if (in != stdin) fclose(in);
            chmod(path, 0600);
            return 0;
        }
    }

    printf("Usage: crontab [ -u user ] file\n       crontab [ -u user ] { -l | -r | -e }\n");
    return 1;
}
