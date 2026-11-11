#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CRON_VERSION "3.0pl1-137 (akaOS)"
#define SPOOL_DIR "/var/spool/cron"
#define SPOOL_CRONTABS "/var/spool/cron/crontabs"
#define SYSTEM_CRONTAB "/etc/crontab"
#define CRON_D_DIR "/etc/cron.d"

/* Option flags */
static bool opt_foreground = false; /* -n */
static bool opt_permit_any = false;  /* -p */
static bool opt_syslog = false;      /* -s */
static const char *opt_mailcmd = NULL; /* -m */
static unsigned int opt_debug = 0;   /* -x */
static bool mail_disabled = false;

/* Debug flags: ext,sch,proc,pars,load,misc,test,bit */
#define DEBUG_EXT  (1 << 0)
#define DEBUG_SCH  (1 << 1)
#define DEBUG_PROC (1 << 2)
#define DEBUG_PARS (1 << 3)
#define DEBUG_LOAD (1 << 4)
#define DEBUG_MISC (1 << 5)
#define DEBUG_TEST (1 << 6)
#define DEBUG_BIT  (1 << 7)

static void cron_log(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (opt_foreground || (opt_debug & DEBUG_MISC)) {
        vfprintf(stdout, fmt, args);
        fprintf(stdout, "\n");
        fflush(stdout);
    }
    va_end(args);
}

static void print_version(void) {
    printf("cron %s\n", CRON_VERSION);
}

static void print_usage(void) {
    printf("Usage: cron [-n | -p | -s | -m<mailcommand>]\n"
           "       cron -x [ext,sch,proc,pars,load,misc,test,bit]\n"
           "Options:\n"
           "  -s              log output using syslog\n"
           "  -m <command>    specify shell command string for sending mail (or 'off')\n"
           "  -n              run in the foreground\n"
           "  -p              permit any crontab\n"
           "  -x <flags>      enable debugging output\n"
           "      --help      display this help and exit\n"
           "      --version   output version information and exit\n");
}

static void parse_debug_flags(const char *arg) {
    if (!arg) return;
    char buf[128];
    strncpy(buf, arg, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *token = strtok(buf, ",");
    while (token) {
        if (strcmp(token, "ext") == 0) opt_debug |= DEBUG_EXT;
        else if (strcmp(token, "sch") == 0) opt_debug |= DEBUG_SCH;
        else if (strcmp(token, "proc") == 0) opt_debug |= DEBUG_PROC;
        else if (strcmp(token, "pars") == 0) opt_debug |= DEBUG_PARS;
        else if (strcmp(token, "load") == 0) opt_debug |= DEBUG_LOAD;
        else if (strcmp(token, "misc") == 0) opt_debug |= DEBUG_MISC;
        else if (strcmp(token, "test") == 0) opt_debug |= DEBUG_TEST;
        else if (strcmp(token, "bit") == 0) opt_debug |= DEBUG_BIT;
        token = strtok(NULL, ",");
    }
}

/* Parse crontab field: supports numbers, *, step values, lists, ranges */
static bool field_matches(const char *field, int current_val, int min_val, int max_val) {
    (void)max_val;
    if (!field || !*field) return false;
    if (strcmp(field, "*") == 0) return true;

    /* Handle steps */
    if (field[0] == '*' && field[1] == '/') {
        int step = atoi(field + 2);
        if (step > 0 && ((current_val - min_val) % step) == 0) return true;
        return false;
    }

    char buf[64];
    strncpy(buf, field, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *token = strtok(buf, ",");
    while (token) {
        char *dash = strchr(token, '-');
        if (dash) {
            *dash = '\0';
            int start = atoi(token);
            int end = atoi(dash + 1);
            if (current_val >= start && current_val <= end) return true;
        } else {
            if (atoi(token) == current_val) return true;
        }
        token = strtok(NULL, ",");
    }
    return false;
}

/* Execute a cron job */
static void execute_job(const char *user, const char *cmd) {
    cron_log("[cron] executing command for user %s: %s", user ? user : "root", cmd);
    pid_t pid = fork();
    if (pid == 0) {
        /* Establish job credentials before running any user command. */
        struct passwd *account = user ? getpwnam(user) : NULL;
        if (!account) {
            fprintf(stderr, "cron: unknown or missing job user: %s\n",
                    user ? user : "(missing)");
            _exit(126);
        }
        if (setgroups(0, NULL) != 0 || setgid(account->pw_gid) != 0 ||
            setuid(account->pw_uid) != 0) {
            perror("cron: unable to establish job credentials");
            _exit(126);
        }
        char *const args[] = {"/bin/sh", "-c", (char *)cmd, NULL};
        execv("/bin/sh", args);
        _exit(127);
    }
}

/* Parse and process a crontab file */
static void process_crontab_file(const char *filepath, const char *default_user, const struct tm *now) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) return;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\0' || *p == '\n') continue;

        /* Strip trailing newline */
        size_t len = strlen(p);
        if (len > 0 && p[len - 1] == '\n') p[len - 1] = '\0';

        char min[32], hour[32], mday[32], mon[32], wday[32];
        char rest[384];

        /* System crontabs (/etc/crontab, /etc/cron.d files) have a user field */
        if (sscanf(p, "%31s %31s %31s %31s %31s %383[^\n]", min, hour, mday, mon, wday, rest) == 6) {
            char user[32];
            char cmd[350];
            const char *job_user = default_user;
            const char *job_cmd = rest;

            if (!default_user) {
                /* Try splitting rest into user and cmd */
                if (sscanf(rest, "%31s %349[^\n]", user, cmd) == 2) {
                    job_user = user;
                    job_cmd = cmd;
                }
            }

            if (field_matches(min, now->tm_min, 0, 59) &&
                field_matches(hour, now->tm_hour, 0, 23) &&
                field_matches(mday, now->tm_mday, 1, 31) &&
                field_matches(mon, now->tm_mon + 1, 1, 12) &&
                field_matches(wday, now->tm_wday, 0, 6)) {
                execute_job(job_user, job_cmd);
            }
        }
    }
    fclose(fp);
}

/* Check all crontabs */
static void run_cron_cycle(void) {
    time_t t = time(NULL);
    struct tm *now = localtime(&t);
    if (!now) return;

    cron_log("[cron] wake cycle at %02d:%02d", now->tm_hour, now->tm_min);

    /* 1. Process /etc/crontab */
    process_crontab_file(SYSTEM_CRONTAB, NULL, now);

    /* 2. Process /etc/cron.d */
    DIR *d = opendir(CRON_D_DIR);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", CRON_D_DIR, ent->d_name);
            process_crontab_file(path, NULL, now);
        }
        closedir(d);
    }

    /* 3. Process /var/spool/cron/crontabs */
    d = opendir(SPOOL_CRONTABS);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", SPOOL_CRONTABS, ent->d_name);
            process_crontab_file(path, ent->d_name, now);
        }
        closedir(d);
    }

    /* Wait for child processes to avoid zombies */
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);
}

int main(int argc, char **argv) {
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
        if (strcmp(arg, "-n") == 0) {
            opt_foreground = true;
            continue;
        }
        if (strcmp(arg, "-p") == 0) {
            opt_permit_any = true;
            continue;
        }
        if (strcmp(arg, "-s") == 0) {
            opt_syslog = true;
            continue;
        }
        if (strncmp(arg, "-m", 2) == 0) {
            const char *val = (arg[2] != '\0') ? arg + 2 : (i + 1 < argc ? argv[++i] : "off");
            if (strcmp(val, "off") == 0) {
                mail_disabled = true;
            } else {
                opt_mailcmd = val;
            }
            continue;
        }
        if (strcmp(arg, "-x") == 0) {
            if (i + 1 < argc) parse_debug_flags(argv[++i]);
            continue;
        }
        if (strncmp(arg, "-x", 2) == 0) {
            parse_debug_flags(arg + 2);
            continue;
        }
    }

    /* Ensure required spool directories exist */
    mkdir("/var", 0755);
    mkdir("/var/spool", 0755);
    mkdir(SPOOL_DIR, 0755);
    mkdir(SPOOL_CRONTABS, 0730);
    mkdir("/etc", 0755);
    mkdir(CRON_D_DIR, 0755);

    cron_log("cron daemon %s starting", CRON_VERSION);

    /* Test mode run: if run with -x test or in non-daemon one-shot */
    if ((opt_debug & DEBUG_TEST)) {
        cron_log("[cron] test mode executed successfully");
        return 0;
    }

    if (!opt_foreground) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("cron: fork failed");
            return 1;
        }
        if (pid > 0) {
            /* Parent exits immediately */
            return 0;
        }
        setsid();
    }

    /* Main daemon loop */
    while (1) {
        run_cron_cycle();
        sleep(60);
    }

    return 0;
}
