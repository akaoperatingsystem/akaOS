#ifndef AURORA_TYPES_H
#define AURORA_TYPES_H

#include <akaos/display.h>
#include <akaos/input.h>
#include <stdint.h>
#include <stddef.h>

#define PANEL_HEIGHT 48U
#define TASK_WIDTH 42U
#define TASK_GAP 8U
#define LAUNCHER_WIDTH 42U
#define AURORA_MAX_APPS 32U
#define AURORA_SEARCH_MAX 31U

#define CATEGORY_ALL 0
#define CATEGORY_SYSTEM 1
#define CATEGORY_UTILITIES 2
#define CATEGORY_INTERNET 3
#define CATEGORY_COUNT 4

extern int g_light_theme;

struct aurora_clock {
    int month, day, year, hour, minute, second;
    unsigned subticks;
    int valid;
    int utc_mode;
};

struct aurora_network_info {
    int available;
    int connected;
    char ifname[16];
    char ip_addr[24];
    char gateway[24];
    char netmask[24];
    int signal_percent;
};

struct aurora_capabilities {
    struct aurora_network_info net;
    int audio;
    int battery;
};

struct aurora_app {
    char id[24];
    char name[32];
    char executable[48];
    char icon[24];
    int category;
};

struct aurora_launcher {
    struct aurora_app apps[AURORA_MAX_APPS];
    unsigned app_count;
    char search[AURORA_SEARCH_MAX + 1U];
    unsigned search_length;
    unsigned selected;
    int active_category;
    int open;
};

struct aurora_surface {
    int fd;
    size_t size;
    char name[32];
    struct akaos_surface_header *header;
    uint32_t *pixels;
};

struct aurora_calendar {
    int open;
    int year;
    int month;
    int day;
};

struct aurora_net_popup {
    int open;
};

#endif /* AURORA_TYPES_H */
