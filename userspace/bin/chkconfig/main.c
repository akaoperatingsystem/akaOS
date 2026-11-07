#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

#define INIT_D_PATH "/etc/init.d"
#define RC_BASE_PATH "/etc"

static int opt_levels[8] = {0, 0, 1, 1, 1, 1, 0, 0}; // default levels 2, 3, 4, 5
static int custom_levels = 0;

static void print_usage(void) {
    fprintf(stderr, "chkconfig version 1.5.3 - Copyright (C) 1997-2005 Red Hat, Inc.\n"
                    "This may be freely redistributed under the terms of the GNU Public License.\n\n"
                    "usage:   chkconfig [--level <levels>] <name> <on|off|reset|resetpriorities>\n"
                    "         chkconfig [--level <levels>] <name>\n"
                    "         chkconfig --list [<name>]\n"
                    "         chkconfig --add <name>\n"
                    "         chkconfig --del <name>\n");
}

static void ensure_directories(void) {
    mkdir("/etc", 0755);
    mkdir("/etc/init.d", 0755);
    for (int i = 0; i <= 6; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/etc/rc%d.d", i);
        mkdir(path, 0755);
    }
}

struct service_info {
    char name[64];
    int start_levels[8]; // 1 if start, 0 if kill/off
    int priority_start;
    int priority_stop;
    int exists;
};

static int parse_init_script(const char *name, struct service_info *info) {
    memset(info, 0, sizeof(*info));
    strncpy(info->name, name, sizeof(info->name) - 1);
    info->priority_start = 50;
    info->priority_stop = 50;

    char path[128];
    snprintf(path, sizeof(path), "%s/%s", INIT_D_PATH, name);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    info->exists = 1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "# chkconfig:", 12) == 0) {
            p += 12;
            while (*p == ' ' || *p == '\t') p++;
            char levels[32] = "";
            int p_start = 50, p_stop = 50;
            if (sscanf(p, "%31s %d %d", levels, &p_start, &p_stop) >= 1) {
                info->priority_start = p_start;
                info->priority_stop = p_stop;
                if (strcmp(levels, "-") != 0) {
                    for (int i = 0; levels[i]; ++i) {
                        if (levels[i] >= '0' && levels[i] <= '6') {
                            info->start_levels[levels[i] - '0'] = 1;
                        }
                    }
                }
            }
            break;
        }
    }
    fclose(f);
    return 1;
}

static int get_service_status(const char *name, int level) {
    char rc_path[128];
    snprintf(rc_path, sizeof(rc_path), "%s/rc%d.d", RC_BASE_PATH, level);
    DIR *d = opendir(rc_path);
    if (!d) return 0;

    struct dirent *de;
    int status = 0; // 0: off, 1: on
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == 'S' && strcmp(de->d_name + 3, name) == 0) {
            status = 1;
            break;
        }
    }
    closedir(d);
    return status;
}

static void set_service_status(const char *name, int level, int on, int prio_start, int prio_stop) {
    char rc_path[128];
    snprintf(rc_path, sizeof(rc_path), "%s/rc%d.d", RC_BASE_PATH, level);
    mkdir(rc_path, 0755);

    DIR *d = opendir(rc_path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if ((de->d_name[0] == 'S' || de->d_name[0] == 'K') && strcmp(de->d_name + 3, name) == 0) {
                char rm_path[256];
                snprintf(rm_path, sizeof(rm_path), "%s/%s", rc_path, de->d_name);
                unlink(rm_path);
            }
        }
        closedir(d);
    }

    char link_path[256];
    char target_path[256];
    snprintf(target_path, sizeof(target_path), "../init.d/%s", name);
    if (on) {
        snprintf(link_path, sizeof(link_path), "%s/S%02d%s", rc_path, prio_start, name);
    } else {
        snprintf(link_path, sizeof(link_path), "%s/K%02d%s", rc_path, prio_stop, name);
    }
    symlink(target_path, link_path);
}

static int list_services(const char *specific_name) {
    ensure_directories();
    DIR *d = opendir(INIT_D_PATH);
    if (!d) {
        if (specific_name) {
            fprintf(stderr, "error reading information on service %s: No such file or directory\n", specific_name);
            return 1;
        }
        return 0;
    }

    struct dirent *de;
    int found = 0;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (specific_name && strcmp(de->d_name, specific_name) != 0) continue;

        struct service_info info;
        if (!parse_init_script(de->d_name, &info)) continue;

        found = 1;
        printf("%-15s", de->d_name);
        for (int lvl = 0; lvl <= 6; ++lvl) {
            int status = get_service_status(de->d_name, lvl);
            printf("\t%d:%s", lvl, status ? "on" : "off");
        }
        printf("\n");
    }
    closedir(d);

    if (specific_name && !found) {
        fprintf(stderr, "error reading information on service %s: No such file or directory\n", specific_name);
        return 1;
    }
    return 0;
}

static int add_service(const char *name) {
    ensure_directories();
    struct service_info info;
    if (!parse_init_script(name, &info)) {
        fprintf(stderr, "error reading information on service %s: No such file or directory\n", name);
        return 1;
    }

    for (int lvl = 0; lvl <= 6; ++lvl) {
        set_service_status(name, lvl, info.start_levels[lvl], info.priority_start, info.priority_stop);
    }
    return 0;
}

static int del_service(const char *name) {
    ensure_directories();

    for (int lvl = 0; lvl <= 6; ++lvl) {
        char rc_path[128];
        snprintf(rc_path, sizeof(rc_path), "%s/rc%d.d", RC_BASE_PATH, lvl);
        DIR *d = opendir(rc_path);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if ((de->d_name[0] == 'S' || de->d_name[0] == 'K') && strcmp(de->d_name + 3, name) == 0) {
                    char rm_path[256];
                    snprintf(rm_path, sizeof(rm_path), "%s/%s", rc_path, de->d_name);
                    unlink(rm_path);
                }
            }
            closedir(d);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    ensure_directories();
    if (argc < 2) {
        print_usage();
        return 1;
    }

    int idx = 1;
    while (idx < argc) {
        if (strcmp(argv[idx], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[idx], "--list") == 0) {
            const char *svc = (idx + 1 < argc && argv[idx + 1][0] != '-') ? argv[idx + 1] : NULL;
            return list_services(svc);
        } else if (strcmp(argv[idx], "--add") == 0) {
            if (idx + 1 >= argc) {
                print_usage();
                return 1;
            }
            return add_service(argv[idx + 1]);
        } else if (strcmp(argv[idx], "--del") == 0) {
            if (idx + 1 >= argc) {
                print_usage();
                return 1;
            }
            return del_service(argv[idx + 1]);
        } else if (strcmp(argv[idx], "--level") == 0) {
            if (idx + 1 >= argc) {
                print_usage();
                return 1;
            }
            char *lvls = argv[++idx];
            memset(opt_levels, 0, sizeof(opt_levels));
            for (int k = 0; lvls[k]; ++k) {
                if (lvls[k] >= '0' && lvls[k] <= '7') {
                    opt_levels[lvls[k] - '0'] = 1;
                }
            }
            custom_levels = 1;
            idx++;
        } else {
            break;
        }
    }

    if (idx >= argc) {
        print_usage();
        return 1;
    }

    const char *svc_name = argv[idx++];
    if (idx >= argc) {
        // Query mode: check if enabled in specified level (or level 3 by default)
        int check_lvl = 3;
        if (custom_levels) {
            for (int k = 0; k <= 7; ++k) {
                if (opt_levels[k]) { check_lvl = k; break; }
            }
        }
        int status = get_service_status(svc_name, check_lvl);
        return status ? 0 : 1;
    }

    const char *state = argv[idx++];
    struct service_info info;
    if (!parse_init_script(svc_name, &info)) {
        // Even if init script does not exist, allow setting status if created manually
        info.priority_start = 50;
        info.priority_stop = 50;
    }

    if (strcmp(state, "on") == 0) {
        for (int lvl = 0; lvl <= 6; ++lvl) {
            if (opt_levels[lvl]) {
                set_service_status(svc_name, lvl, 1, info.priority_start, info.priority_stop);
            }
        }
        return 0;
    } else if (strcmp(state, "off") == 0) {
        for (int lvl = 0; lvl <= 6; ++lvl) {
            if (opt_levels[lvl]) {
                set_service_status(svc_name, lvl, 0, info.priority_start, info.priority_stop);
            }
        }
        return 0;
    } else if (strcmp(state, "reset") == 0) {
        // Reset to defaults across all levels
        for (int lvl = 0; lvl <= 6; ++lvl) {
            set_service_status(svc_name, lvl, info.start_levels[lvl], info.priority_start, info.priority_stop);
        }
        return 0;
    } else {
        print_usage();
        return 1;
    }
}
