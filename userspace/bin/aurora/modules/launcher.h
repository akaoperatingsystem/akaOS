#ifndef AURORA_LAUNCHER_H
#define AURORA_LAUNCHER_H

#include "aurora_types.h"
#include "draw.h"

static inline int read_text_file(const char *path, char *buffer, size_t capacity) {
    if (!path || !buffer || capacity < 2U) return 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t size = read(fd, buffer, capacity - 1U);
    close(fd);
    if (size <= 0) return 0;
    buffer[size] = '\0';
    return 1;
}

static inline void copy_field(char *target, size_t capacity,
                              const char *begin, const char *end) {
    if (!target || capacity == 0U) return;
    size_t length = end > begin ? (size_t)(end - begin) : 0U;
    if (length >= capacity) length = capacity - 1U;
    if (length) memcpy(target, begin, length);
    target[length] = '\0';
}

static inline int parse_category(const char *cat_str) {
    if (!cat_str) return CATEGORY_ALL;
    if (strcmp(cat_str, "System") == 0) return CATEGORY_SYSTEM;
    if (strcmp(cat_str, "Utilities") == 0) return CATEGORY_UTILITIES;
    if (strcmp(cat_str, "Internet") == 0) return CATEGORY_INTERNET;
    return CATEGORY_ALL;
}

static inline const char *category_name(int cat) {
    switch (cat) {
    case CATEGORY_SYSTEM: return "System";
    case CATEGORY_UTILITIES: return "Utilities";
    case CATEGORY_INTERNET: return "Internet";
    default: return "All";
    }
}

static inline void launcher_load_registry(struct aurora_launcher *launcher) {
    char registry[2048];
    memset(launcher, 0, sizeof(*launcher));
    launcher->active_category = CATEGORY_ALL;

    if (!read_text_file("/etc/aurora-apps", registry, sizeof(registry))) {
        /* Fallback builtins if registry file is unreadable */
        strcpy(launcher->apps[0].id, "terminal");
        strcpy(launcher->apps[0].name, "Terminal");
        strcpy(launcher->apps[0].executable, "/bin/terminal");
        strcpy(launcher->apps[0].icon, "terminal");
        launcher->apps[0].category = CATEGORY_SYSTEM;

        strcpy(launcher->apps[1].id, "notepad");
        strcpy(launcher->apps[1].name, "Text Editor");
        strcpy(launcher->apps[1].executable, "/bin/notepad");
        strcpy(launcher->apps[1].icon, "notepad");
        launcher->apps[1].category = CATEGORY_UTILITIES;

        strcpy(launcher->apps[2].id, "taskmgr");
        strcpy(launcher->apps[2].name, "Task Manager");
        strcpy(launcher->apps[2].executable, "/bin/taskmgr");
        strcpy(launcher->apps[2].icon, "taskmgr");
        launcher->apps[2].category = CATEGORY_SYSTEM;

        strcpy(launcher->apps[3].id, "akaosconfig");
        strcpy(launcher->apps[3].name, "Settings");
        strcpy(launcher->apps[3].executable, "/bin/akaos-config");
        strcpy(launcher->apps[3].icon, "akaos-config");
        launcher->apps[3].category = CATEGORY_SYSTEM;

        launcher->app_count = 4;
        return;
    }

    const char *line = registry;
    while (*line && launcher->app_count < AURORA_MAX_APPS) {
        const char *end = strchr(line, '\n');
        if (!end) end = line + strlen(line);
        if (*line && *line != '#') {
            const char *first = strchr(line, '|');
            const char *second = first ? strchr(first + 1, '|') : NULL;
            const char *third = second ? strchr(second + 1, '|') : NULL;
            const char *fourth = third ? strchr(third + 1, '|') : NULL;
            if (first && second && third && first < end && second < end && third < end) {
                struct aurora_app *app = &launcher->apps[launcher->app_count++];
                copy_field(app->id, sizeof(app->id), line, first);
                copy_field(app->name, sizeof(app->name), first + 1, second);
                copy_field(app->executable, sizeof(app->executable), second + 1, third);
                if (fourth && fourth < end) {
                    copy_field(app->icon, sizeof(app->icon), third + 1, fourth);
                    char cat_buf[16];
                    copy_field(cat_buf, sizeof(cat_buf), fourth + 1, end);
                    app->category = parse_category(cat_buf);
                } else {
                    copy_field(app->icon, sizeof(app->icon), third + 1, end);
                    app->category = CATEGORY_ALL;
                }
            }
        }
        line = *end ? end + 1 : end;
    }
}

static inline int ascii_contains_casefold(const char *text, const char *query) {
    if (!query || !*query) return 1;
    if (!text) return 0;
    for (size_t start = 0; text[start]; ++start) {
        size_t offset = 0;
        while (query[offset] && text[start + offset]) {
            char a = text[start + offset];
            char b = query[offset];
            if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
            if (a != b) break;
            ++offset;
        }
        if (!query[offset]) return 1;
    }
    return 0;
}

static inline int app_matches(const struct aurora_app *app, int category, const char *search) {
    if (category != CATEGORY_ALL && app->category != category && app->category != CATEGORY_ALL) {
        return 0;
    }
    return ascii_contains_casefold(app->name, search) ||
           ascii_contains_casefold(app->id, search);
}

static inline unsigned launcher_match_count(const struct aurora_launcher *launcher) {
    unsigned count = 0;
    for (unsigned i = 0; i < launcher->app_count; ++i) {
        if (app_matches(&launcher->apps[i], launcher->active_category, launcher->search)) {
            ++count;
        }
    }
    return count;
}

static inline const struct aurora_app *launcher_match_at(
    const struct aurora_launcher *launcher, unsigned wanted) {
    unsigned found = 0;
    for (unsigned i = 0; i < launcher->app_count; ++i) {
        if (!app_matches(&launcher->apps[i], launcher->active_category, launcher->search)) continue;
        if (found++ == wanted) return &launcher->apps[i];
    }
    return NULL;
}

static inline void launch_application(const struct aurora_app *app) {
    if (!app || app->executable[0] != '/') return;
    pid_t child = fork();
    if (child == 0) {
        char *arguments[] = {(char *)app->executable, NULL};
        execv(app->executable, arguments);
        _exit(127);
    }
}

static inline void render_launcher(uint32_t *pixels, uint32_t width, uint32_t height,
                                   const struct aurora_launcher *launcher) {
    /* Modern unified application menu container */
    fill_rounded_rect(pixels, width, height, 0, 0, (int)width, (int)height, 14,
                      g_light_theme ? UINT32_C(0xf4f7fb) : UINT32_C(0x141c29),
                      g_light_theme ? UINT32_C(0xbac5d3) : UINT32_C(0x50617a));

    /* Header title */
    draw_ui_text(pixels, width, height, 18, 12, 1, "Applications",
                 g_light_theme ? UINT32_C(0x1c2a3e) : UINT32_C(0xf4f7fc),
                 (int)width - 36);

    /* Search Bar with Magnifier */
    fill_rounded_rect(pixels, width, height, 14, 36, (int)width - 28, 30, 7,
                      g_light_theme ? UINT32_C(0xffffff) : UINT32_C(0x202c3e),
                      g_light_theme ? UINT32_C(0xc3cedc) : UINT32_C(0x465873));
    fill_rounded_rect(pixels, width, height, 22, 44, 9, 9, 4,
                      UINT32_C(0x202c3e), UINT32_C(0x9eacc0));
    fill_rect(pixels, width, height, 30, 52, 6, 2, UINT32_C(0x9eacc0));

    const char *query = launcher->search_length ? launcher->search : "Search applications...";
    draw_ui_text(pixels, width, height, 44, 43, 1, query,
                 launcher->search_length
                     ? (g_light_theme ? UINT32_C(0x243348) : UINT32_C(0xf0f4fa))
                     : UINT32_C(0x748298),
                 (int)width - 64);

    /* Category Filter Tabs: All | System | Utilities | Internet */
    const char *tabs[CATEGORY_COUNT] = {"All", "System", "Utilities", "Internet"};
    int tab_x = 14;
    int tab_y = 72;
    int tab_w = ((int)width - 28) / CATEGORY_COUNT;
    for (int c = 0; c < CATEGORY_COUNT; ++c) {
        int active = (launcher->active_category == c);
        fill_rounded_rect(pixels, width, height, tab_x, tab_y, tab_w - 4, 22, 5,
                          active ? (g_light_theme ? UINT32_C(0xdbe9fb) : UINT32_C(0x2b3e59))
                                 : (g_light_theme ? UINT32_C(0xe8edf4) : UINT32_C(0x192333)),
                          active ? UINT32_C(0x628fd0) : UINT32_C(0x384a62));
        draw_ui_text(pixels, width, height, tab_x + 6, tab_y + 4, 1, tabs[c],
                     active ? (g_light_theme ? UINT32_C(0x19283d) : UINT32_C(0xffffff))
                            : (g_light_theme ? UINT32_C(0x5a6a7e) : UINT32_C(0x94a4b8)),
                     tab_w - 12);
        tab_x += tab_w;
    }

    /* Application Item Rows */
    unsigned matches = launcher_match_count(launcher);
    if (matches == 0U) {
        draw_ui_text(pixels, width, height, 20, 115, 1, "No matching applications",
                     UINT32_C(0x9eacc0), (int)width - 40);
        return;
    }

    int list_y_start = 100;
    unsigned item_h = 36;
    unsigned visible = (height > (unsigned)list_y_start + 10)
                           ? (height - (unsigned)list_y_start - 10) / item_h
                           : 0U;
    if (visible > matches) visible = matches;
    unsigned first = launcher->selected >= visible ? launcher->selected - visible + 1U : 0U;

    for (unsigned row = 0; row < visible; ++row) {
        unsigned match_index = first + row;
        const struct aurora_app *app = launcher_match_at(launcher, match_index);
        if (!app) break;
        int y = list_y_start + (int)row * (int)item_h;
        if (match_index == launcher->selected) {
            fill_rounded_rect(pixels, width, height, 12, y, (int)width - 24, (int)item_h - 4, 8,
                              g_light_theme ? UINT32_C(0xdbe9fb) : UINT32_C(0x2b3e59),
                              g_light_theme ? UINT32_C(0x76a5da) : UINT32_C(0x4f79aa));
        }
        draw_app_icon(pixels, width, height, 18, y + 2, app->icon);
        draw_ui_text(pixels, width, height, 54, y + 8, 1, app->name,
                     g_light_theme ? UINT32_C(0x26364b) : UINT32_C(0xeaf0f8),
                     (int)width - 130);

        /* Category badge on the right */
        const char *cname = category_name(app->category);
        draw_ui_text(pixels, width, height, (int)width - 75, y + 9, 1, cname,
                     UINT32_C(0x6e8098), 65);
    }
}

#endif /* AURORA_LAUNCHER_H */
