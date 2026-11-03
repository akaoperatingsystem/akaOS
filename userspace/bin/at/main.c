#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define AT_SPOOL_DIR "/var/spool/at"
#define AT_SPOOL_SUB "/var/spool/at/spool"
#define AT_ALLOW_FILE "/etc/at.allow"
#define AT_DENY_FILE "/etc/at.deny"
#define MAX_JOB_CMDS 4096
#define MAX_LINE 1024

enum at_mode {
    MODE_AT,
    MODE_BATCH,
    MODE_ATQ,
    MODE_ATRM,
    MODE_CAT
};

static enum at_mode current_mode = MODE_AT;
static char opt_queue = 'a';
static const char *opt_file = NULL;
static int opt_mail = 0;
static int opt_verbose = 0;
static const char *opt_time_arg = NULL;

static void print_version(void) {
    fprintf(stderr, "at version 3.1.20 (akaOS native implementation)\n");
}

static void print_usage(const char *progname) {
    if (strstr(progname, "atq") || current_mode == MODE_ATQ) {
        printf("Usage: atq [-V] [-q queue]\n");
    } else if (strstr(progname, "atrm") || current_mode == MODE_ATRM) {
        printf("Usage: atrm [-V] job [job...]\n");
    } else if (strstr(progname, "batch") || current_mode == MODE_BATCH) {
        printf("Usage: batch [-V] [-q queue] [-f file] [-mv]\n");
    } else {
        printf("Usage: at [-V] [-q queue] [-f file] [-mldbv] TIME\n"
               "       at [-V] [-q queue] [-f file] [-mldbv] -t time_arg\n"
               "       at -c job [job...]\n"
               "       atq [-V] [-q queue]\n"
               "       atrm [-V] job [job...]\n"
               "       batch\n");
    }
}

static int check_permissions(const char *username, uid_t uid) {
    if (uid == 0 || (username && strcmp(username, "root") == 0)) {
        return 1;
    }
    struct stat st;
    if (stat(AT_ALLOW_FILE, &st) == 0) {
        FILE *f = fopen(AT_ALLOW_FILE, "r");
        if (!f) return 0;
        char line[128];
        int allowed = 0;
        while (fgets(line, sizeof(line), f)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n' || line[l - 1] == ' ' || line[l - 1] == '\t')) {
                line[--l] = '\0';
            }
            if (line[0] == '#' || line[0] == '\0') continue;
            if (username && strcmp(line, username) == 0) {
                allowed = 1;
                break;
            }
        }
        fclose(f);
        return allowed;
    }
    if (stat(AT_DENY_FILE, &st) == 0) {
        FILE *f = fopen(AT_DENY_FILE, "r");
        if (!f) return 1;
        char line[128];
        int denied = 0;
        while (fgets(line, sizeof(line), f)) {
            size_t l = strlen(line);
            while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n' || line[l - 1] == ' ' || line[l - 1] == '\t')) {
                line[--l] = '\0';
            }
            if (line[0] == '#' || line[0] == '\0') continue;
            if (username && strcmp(line, username) == 0) {
                denied = 1;
                break;
            }
        }
        fclose(f);
        return !denied;
    }
    return 0;
}

static int parse_touch_time(const char *arg, time_t *target_time) {
    size_t len = strlen(arg);
    if (len != 8 && len != 10 && len != 12) return 0;
    int year, mon, day, hour, min;
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if (len == 8) {
        year = tm_now.tm_year + 1900;
        if (sscanf(arg, "%2d%2d%2d%2d", &mon, &day, &hour, &min) != 4) return 0;
    } else if (len == 10) {
        int y2;
        if (sscanf(arg, "%2d%2d%2d%2d%2d", &y2, &mon, &day, &hour, &min) != 5) return 0;
        year = (y2 >= 69) ? (1900 + y2) : (2000 + y2);
    } else {
        if (sscanf(arg, "%4d%2d%2d%2d%2d", &year, &mon, &day, &hour, &min) != 5) return 0;
    }
    struct tm tm_t;
    memset(&tm_t, 0, sizeof(tm_t));
    tm_t.tm_year = year - 1900;
    tm_t.tm_mon = mon - 1;
    tm_t.tm_mday = day;
    tm_t.tm_hour = hour;
    tm_t.tm_min = min;
    tm_t.tm_sec = 0;
    tm_t.tm_isdst = -1;
    *target_time = mktime(&tm_t);
    return 1;
}

static int parse_time_words(int argc, char **argv, int start_idx, time_t *target_time) {
    time_t now = time(NULL);
    struct tm tm_target;
    localtime_r(&now, &tm_target);
    tm_target.tm_sec = 0;
    tm_target.tm_isdst = -1;
    int idx = start_idx;
    if (idx >= argc) return 0;
    int hour = -1, min = 0;
    const char *first = argv[idx];
    if (strcasecmp(first, "now") == 0) {
        hour = tm_target.tm_hour;
        min = tm_target.tm_min;
        idx++;
    } else if (strcasecmp(first, "midnight") == 0) {
        hour = 0; min = 0;
        idx++;
    } else if (strcasecmp(first, "noon") == 0) {
        hour = 12; min = 0;
        idx++;
    } else if (strcasecmp(first, "teatime") == 0) {
        hour = 16; min = 0;
        idx++;
    } else {
        char tstr[64];
        strncpy(tstr, first, sizeof(tstr) - 1);
        tstr[sizeof(tstr) - 1] = '\0';
        int is_pm = -1;
        size_t tlen = strlen(tstr);
        if (tlen > 2) {
            if (strcasecmp(tstr + tlen - 2, "am") == 0) {
                is_pm = 0;
                tstr[tlen - 2] = '\0';
            } else if (strcasecmp(tstr + tlen - 2, "pm") == 0) {
                is_pm = 1;
                tstr[tlen - 2] = '\0';
            }
        }
        char *colon = strchr(tstr, ':');
        if (colon) {
            *colon = '\0';
            hour = atoi(tstr);
            min = atoi(colon + 1);
        } else {
            hour = atoi(tstr);
            min = 0;
        }
        if (is_pm == 1 && hour < 12) hour += 12;
        if (is_pm == 0 && hour == 12) hour = 0;
        idx++;
        if (idx < argc && (strcasecmp(argv[idx], "am") == 0 || strcasecmp(argv[idx], "pm") == 0)) {
            if (strcasecmp(argv[idx], "pm") == 0 && hour < 12) hour += 12;
            if (strcasecmp(argv[idx], "am") == 0 && hour == 12) hour = 0;
            idx++;
        }
    }
    if (hour < 0 || hour > 23 || min < 0 || min > 59) {
        return 0;
    }
    tm_target.tm_hour = hour;
    tm_target.tm_min = min;
    long delta_seconds = 0;
    int day_offset = 0;
    while (idx < argc) {
        const char *w = argv[idx];
        if (strcasecmp(w, "today") == 0) {
            idx++;
            continue;
        }
        if (strcasecmp(w, "tomorrow") == 0) {
            day_offset += 1;
            idx++;
            continue;
        }
        if (strcmp(w, "+") == 0 && idx + 2 < argc) {
            int count = atoi(argv[idx + 1]);
            const char *unit = argv[idx + 2];
            if (strncasecmp(unit, "minute", 6) == 0) delta_seconds += (long)count * 60;
            else if (strncasecmp(unit, "hour", 4) == 0) delta_seconds += (long)count * 3600;
            else if (strncasecmp(unit, "day", 3) == 0) delta_seconds += (long)count * 86400;
            else if (strncasecmp(unit, "week", 4) == 0) delta_seconds += (long)count * 604800;
            idx += 3;
            continue;
        }
        static const char *month_names[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        int matched_month = -1;
        for (int m = 0; m < 12; ++m) {
            if (strncasecmp(w, month_names[m], 3) == 0) {
                matched_month = m;
                break;
            }
        }
        if (matched_month >= 0 && idx + 1 < argc) {
            tm_target.tm_mon = matched_month;
            tm_target.tm_mday = atoi(argv[idx + 1]);
            idx += 2;
            if (idx < argc && isdigit((unsigned char)argv[idx][0])) {
                int yr = atoi(argv[idx]);
                if (yr > 1900) tm_target.tm_year = yr - 1900;
                idx++;
            }
            continue;
        }
        break;
    }
    time_t calculated = mktime(&tm_target);
    if (day_offset > 0) calculated += (time_t)day_offset * 86400;
    calculated += (time_t)delta_seconds;
    if (calculated <= now && day_offset == 0 && delta_seconds == 0) {
        calculated += 86400;
    }
    *target_time = calculated;
    return 1;
}

static void ensure_spool_dirs(void) {
    mkdir("/var", 0755);
    mkdir("/var/spool", 0755);
    mkdir(AT_SPOOL_DIR, 0770);
    mkdir(AT_SPOOL_SUB, 0700);
}

static int get_next_job_id(void) {
    ensure_spool_dirs();
    char seq_path[128];
    snprintf(seq_path, sizeof(seq_path), "%s/.seq", AT_SPOOL_DIR);
    int fd = open(seq_path, O_RDWR | O_CREAT, 0600);
    int next_id = 1;
    if (fd >= 0) {
        char buf[32];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            next_id = atoi(buf) + 1;
        }
        lseek(fd, 0, SEEK_SET);
        char out[32];
        int len = snprintf(out, sizeof(out), "%d\n", next_id);
        (void)write(fd, out, (size_t)len);
        close(fd);
    }
    return next_id;
}

static int do_atrm(int argc, char **argv, int start_idx) {
    int had_error = 0;
    ensure_spool_dirs();
    for (int i = start_idx; i < argc; ++i) {
        int jid = atoi(argv[i]);
        if (jid <= 0) {
            fprintf(stderr, "atrm: invalid job number '%s'\n", argv[i]);
            had_error = 1;
            continue;
        }
        char filename[256];
        snprintf(filename, sizeof(filename), "%s/%d", AT_SPOOL_SUB, jid);
        if (unlink(filename) != 0) {
            fprintf(stderr, "atrm: cannot remove job %d: %s\n", jid, strerror(errno));
            had_error = 1;
        }
    }
    return had_error ? 1 : 0;
}

static int do_atq(char queue_filter) {
    ensure_spool_dirs();
    int dir_fd = open(AT_SPOOL_SUB, O_RDONLY);
    if (dir_fd < 0) return 0;
    struct stat st;
    if (fstat(dir_fd, &st) != 0 || !S_ISDIR(st.st_mode)) {
        close(dir_fd);
        return 0;
    }
    for (int id = 1; id < 1000; ++id) {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%d", AT_SPOOL_SUB, id);
        FILE *f = fopen(filepath, "r");
        if (!f) continue;
        char line[256];
        time_t job_time = 0;
        char job_queue = 'a';
        char job_user[64] = "root";
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "# AT_TIME=", 10) == 0) {
                job_time = (time_t)strtoul(line + 10, NULL, 10);
            } else if (strncmp(line, "# AT_QUEUE=", 11) == 0) {
                job_queue = line[11];
            } else if (strncmp(line, "# AT_USER=", 10) == 0) {
                size_t ul = strlen(line + 10);
                while (ul > 0 && (line[10 + ul - 1] == '\r' || line[10 + ul - 1] == '\n')) line[10 + --ul] = '\0';
                strncpy(job_user, line + 10, sizeof(job_user) - 1);
            } else if (line[0] != '#') {
                break;
            }
        }
        fclose(f);
        if (queue_filter && queue_filter != '=' && job_queue != queue_filter) {
            continue;
        }
        struct tm tm_exec;
        localtime_r(&job_time, &tm_exec);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%a %b %e %H:%M:%S %Y", &tm_exec);
        printf("%d\t%s %c %s\n", id, time_str, job_queue, job_user);
    }
    close(dir_fd);
    return 0;
}

static int do_cat(int argc, char **argv, int start_idx) {
    ensure_spool_dirs();
    for (int i = start_idx; i < argc; ++i) {
        int jid = atoi(argv[i]);
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%d", AT_SPOOL_SUB, jid);
        FILE *f = fopen(filepath, "r");
        if (!f) {
            fprintf(stderr, "at: cannot open job %d: %s\n", jid, strerror(errno));
            continue;
        }
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            fputs(line, stdout);
        }
        fclose(f);
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *progname = argv[0];
    const char *slash = strrchr(progname, '/');
    if (slash) progname = slash + 1;
    if (strcmp(progname, "atq") == 0) current_mode = MODE_ATQ;
    else if (strcmp(progname, "atrm") == 0) current_mode = MODE_ATRM;
    else if (strcmp(progname, "batch") == 0) {
        current_mode = MODE_BATCH;
        opt_queue = 'b';
    }
    int time_start_idx = -1;
    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        }
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(progname);
            return 0;
        }
        if (strcmp(arg, "-l") == 0) {
            current_mode = MODE_ATQ;
            continue;
        }
        if (strcmp(arg, "-d") == 0) {
            current_mode = MODE_ATRM;
            continue;
        }
        if (strcmp(arg, "-c") == 0) {
            current_mode = MODE_CAT;
            continue;
        }
        if (strcmp(arg, "-m") == 0) {
            opt_mail = 1;
            continue;
        }
        if (strcmp(arg, "-v") == 0) {
            opt_verbose = 1;
            continue;
        }
        if (strcmp(arg, "-q") == 0 && i + 1 < argc) {
            opt_queue = argv[++i][0];
            continue;
        }
        if (strncmp(arg, "-q", 2) == 0 && arg[2] != '\0') {
            opt_queue = arg[2];
            continue;
        }
        if (strcmp(arg, "-f") == 0 && i + 1 < argc) {
            opt_file = argv[++i];
            continue;
        }
        if (strncmp(arg, "-f", 2) == 0 && arg[2] != '\0') {
            opt_file = arg + 2;
            continue;
        }
        if (strcmp(arg, "-t") == 0 && i + 1 < argc) {
            opt_time_arg = argv[++i];
            continue;
        }
        if (strncmp(arg, "-t", 2) == 0 && arg[2] != '\0') {
            opt_time_arg = arg + 2;
            continue;
        }
        if (arg[0] != '-') {
            if (time_start_idx < 0) {
                time_start_idx = i;
            }
            continue;
        }
    }
    uid_t uid = geteuid();
    const char *username = getenv("USER");
    if (!username) username = (uid == 0) ? "root" : "user";
    if (!check_permissions(username, uid)) {
        fprintf(stderr, "You do not have permission to use at.\n");
        return 1;
    }
    if (current_mode == MODE_ATQ) {
        return do_atq(opt_queue != 'a' ? opt_queue : 0);
    }
    if (current_mode == MODE_ATRM) {
        if (time_start_idx < 0) {
            fprintf(stderr, "atrm: no job specified\n");
            return 1;
        }
        return do_atrm(argc, argv, time_start_idx);
    }
    if (current_mode == MODE_CAT) {
        if (time_start_idx < 0) {
            fprintf(stderr, "at: -c requires a job number\n");
            return 1;
        }
        return do_cat(argc, argv, time_start_idx);
    }
    time_t target_time = 0;
    if (current_mode == MODE_BATCH) {
        target_time = time(NULL);
    } else if (opt_time_arg) {
        if (!parse_touch_time(opt_time_arg, &target_time)) {
            fprintf(stderr, "at: invalid touch time format '%s'\n", opt_time_arg);
            return 1;
        }
    } else {
        if (time_start_idx < 0) {
            fprintf(stderr, "at: syntax error: time missing\n");
            print_usage(progname);
            return 1;
        }
        if (!parse_time_words(argc, argv, time_start_idx, &target_time)) {
            fprintf(stderr, "at: invalid time specification\n");
            return 1;
        }
    }
    if (opt_verbose) {
        struct tm tm_exec;
        localtime_r(&target_time, &tm_exec);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%a %b %e %H:%M:%S %Y", &tm_exec);
        fprintf(stderr, "%s\n", time_str);
    }
    char commands[MAX_JOB_CMDS];
    size_t cmd_len = 0;
    if (opt_file) {
        int fd = open(opt_file, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "at: cannot open %s: %s\n", opt_file, strerror(errno));
            return 1;
        }
        ssize_t n = read(fd, commands, sizeof(commands) - 1);
        close(fd);
        if (n >= 0) cmd_len = (size_t)n;
        commands[cmd_len] = '\0';
    } else {
        int is_tty = isatty(STDIN_FILENO);
        if (is_tty) {
            write(STDOUT_FILENO, "at> ", 4);
        }
        char line[512];
        while (fgets(line, sizeof(line), stdin)) {
            size_t llen = strlen(line);
            if (cmd_len + llen < sizeof(commands) - 1) {
                memcpy(commands + cmd_len, line, llen);
                cmd_len += llen;
            }
            if (is_tty) {
                write(STDOUT_FILENO, "at> ", 4);
            }
        }
        commands[cmd_len] = '\0';
        if (is_tty) {
            write(STDOUT_FILENO, "<EOT>\n", 6);
        }
    }
    if (cmd_len == 0) {
        fprintf(stderr, "at: empty job\n");
        return 1;
    }
    int job_id = get_next_job_id();
    char job_path[256];
    snprintf(job_path, sizeof(job_path), "%s/%d", AT_SPOOL_SUB, job_id);
    FILE *out = fopen(job_path, "w");
    if (!out) {
        fprintf(stderr, "at: cannot create job file %s: %s\n", job_path, strerror(errno));
        return 1;
    }
    fprintf(out, "#!/bin/akash\n");
    fprintf(out, "# AT_JOB_ID=%d\n", job_id);
    fprintf(out, "# AT_TIME=%lu\n", (unsigned long)target_time);
    fprintf(out, "# AT_QUEUE=%c\n", opt_queue);
    fprintf(out, "# AT_USER=%s\n", username);
    fprintf(out, "# AT_MAIL=%d\n", opt_mail);
    char cwd[256];
    if (getcwd(cwd, sizeof(cwd))) {
        fprintf(out, "cd '%s'\n", cwd);
    }
    fprintf(out, "%s\n", commands);
    fclose(out);
    chmod(job_path, 0700);
    struct tm tm_exec;
    localtime_r(&target_time, &tm_exec);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%a %b %e %H:%M:%S %Y", &tm_exec);
    fprintf(stderr, "job %d at %s\n", job_id, time_str);
    return 0;
}
