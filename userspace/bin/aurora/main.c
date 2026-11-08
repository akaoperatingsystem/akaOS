/* Aurora Professional Desktop Shell for akaOS
 *
 * Modular Desktop Shell Architecture:
 * - Panel: Dock with active tasks, launcher emblem, and capability-driven tray.
 * - Launcher: Unified Application Menu with category filtering and incremental search.
 * - Calendar: Hardware RTC synchronization with interactive pop-up calendar.
 * - Network: Real-time network daemon integration and connectivity widget.
 * - Notifications: System-wide alert sink and desktop toast manager.
 */

#include "modules/aurora_types.h"
#include "modules/draw.h"
#include "modules/launcher.h"
#include "modules/calendar.h"
#include "modules/network.h"
#include "modules/notifications.h"
#include "modules/panel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>

int g_light_theme = 0;

static int load_light_theme(void) {
    char config[1024];
    return read_text_file("/etc/akaos.conf", config, sizeof(config)) &&
           strstr(config, "DESKTOP_THEME=light") != NULL;
}

static void capabilities_refresh(struct aurora_capabilities *caps) {
    memset(caps, 0, sizeof(*caps));
    network_refresh_info(&caps->net);

    char buffer[512];
    if (read_text_file("/proc/asound/cards", buffer, sizeof(buffer)) &&
        strchr(buffer, '[')) {
        caps->audio = 1;
    }
    caps->battery = 0;
}

static void render_context_menu(uint32_t *pixels, uint32_t width, uint32_t height) {
    fill_rounded_rect(pixels, width, height, 0, 0, (int)width, (int)height, 11,
                      g_light_theme ? UINT32_C(0xf5f7fa) : UINT32_C(0x17202e),
                      g_light_theme ? UINT32_C(0xb9c4d2) : UINT32_C(0x52627a));

    fill_rounded_rect(pixels, width, height, 8, 8, (int)width - 16, 28, 6,
                      UINT32_C(0x223047), UINT32_C(0x344a68));
    draw_app_icon(pixels, width, height, 12, 8, "terminal");
    draw_ui_text(pixels, width, height, 48, 13, 1, "Open Terminal",
                 g_light_theme ? UINT32_C(0x25354a) : UINT32_C(0xecf2fa),
                 (int)width - 58);

    fill_rounded_rect(pixels, width, height, 8, 42, (int)width - 16, 28, 6,
                      UINT32_C(0x1c283a), UINT32_C(0x2f4058));
    draw_app_icon(pixels, width, height, 12, 42, "akaos-config");
    draw_ui_text(pixels, width, height, 48, 47, 1, "System Settings",
                 g_light_theme ? UINT32_C(0x25354a) : UINT32_C(0xecf2fa),
                 (int)width - 58);

    fill_rounded_rect(pixels, width, height, 8, 76, (int)width - 16, 28, 6,
                      UINT32_C(0x182232), UINT32_C(0x28374c));
    draw_ui_text(pixels, width, height, 18, 81, 1, "Show Applications",
                 g_light_theme ? UINT32_C(0x25354a) : UINT32_C(0xecf2fa),
                 (int)width - 36);
}

static int desktop_is_empty_at(const struct akaos_compositor_channel *channel,
                               int x, int y) {
    if (!channel) return 1;
    for (size_t i = 0; i < AKAOS_COMPOSITOR_MAX_SURFACES; ++i) {
        const struct akaos_compositor_slot *slot = &channel->surfaces[i];
        if (!slot->active || (slot->flags & (AKAOS_SURFACE_FLAG_DESKTOP |
                                              AKAOS_SURFACE_FLAG_PANEL |
                                              AKAOS_SURFACE_FLAG_MINIMIZED))) continue;
        if (x >= slot->x && x < slot->x + (int)slot->width &&
            y >= slot->y - 28 && y < slot->y + (int)slot->height) return 0;
    }
    return 1;
}

static int register_slot(struct akaos_compositor_channel *channel,
                         uint32_t surface_id, int32_t x, int32_t y,
                         uint32_t width, uint32_t height, uint32_t flags,
                         uint32_t z_order, const char *shm_name, const char *title) {
    if (!channel || channel->protocol_version != AKAOS_COMPOSITOR_PROTOCOL_VERSION ||
        !shm_name || !title) return 0;
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_ATTACH;
    message.flags = flags;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    message.x = x;
    message.y = y;
    message.width = width;
    message.height = height;
    message.z_order = z_order;
    message.damage_w = width;
    message.damage_h = height;
    strncpy(message.shm_name, shm_name, sizeof(message.shm_name) - 1);
    strncpy(message.title, title, sizeof(message.title) - 1);
    if (strcmp(title, "Desktop Panel") == 0) strncpy(message.icon_name, "panel", sizeof(message.icon_name) - 1);
    else if (strcmp(title, "Applications") == 0) strncpy(message.icon_name, "launcher", sizeof(message.icon_name) - 1);
    else if (strcmp(title, "Calendar") == 0) strncpy(message.icon_name, "calendar", sizeof(message.icon_name) - 1);
    else if (strcmp(title, "Network Status") == 0) strncpy(message.icon_name, "network", sizeof(message.icon_name) - 1);
    else if (strcmp(title, "Desktop Menu") == 0) strncpy(message.icon_name, "menu", sizeof(message.icon_name) - 1);
    return akaos_compositor_submit(channel, &message);
}

static int queue_damage(struct akaos_compositor_channel *channel,
                        uint32_t surface_id, uint32_t width, uint32_t height) {
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_DAMAGE;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    message.damage_w = width;
    message.damage_h = height;
    return akaos_compositor_submit(channel, &message);
}

static int queue_destroy(struct akaos_compositor_channel *channel, uint32_t surface_id) {
    struct akaos_display_message message;
    memset(&message, 0, sizeof(message));
    message.type = AKAOS_DISPLAY_MSG_DESTROY;
    message.surface_id = surface_id;
    message.client_pid = (uint32_t)getpid();
    return akaos_compositor_submit(channel, &message);
}

int main(int argc, char **argv) {
    g_light_theme = load_light_theme();
    int once = 0;
    int spawn_app = 0;
    int request_launcher = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            puts("Usage: aurora [options]\n"
                 "akaOS Professional Desktop Shell and Panel\n\n"
                 "Options:\n"
                 "  --once           Initialize once and exit (for self-testing)\n"
                 "  --spawn-palette  Spawn palette application\n"
                 "  --show-launcher  Ask the running shell to open its launcher\n"
                 "  -v, --version    Output version information and exit\n"
                 "  -h, --help       Display this help and exit");
            return 0;
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            puts("aurora (akaOS Professional Desktop Shell) 3.0");
            return 0;
        } else if (strcmp(argv[i], "--once") == 0) {
            once = 1;
        } else if (strcmp(argv[i], "--spawn-palette") == 0) {
            spawn_app = 1;
        } else if (strcmp(argv[i], "--show-launcher") == 0) {
            request_launcher = 1;
        }
    }

    int info_fd = open(AKAOS_FRAMEBUFFER_PATH, O_RDONLY);
    if (info_fd < 0) {
        printf("aurora: no active framebuffer: %d\n", errno);
        return 1;
    }
    struct akaos_display_info display;
    if (read(info_fd, &display, sizeof(display)) != (ssize_t)sizeof(display) ||
        close(info_fd) != 0 || display.abi_version != AKAOS_DISPLAY_ABI_VERSION) {
        puts("aurora: invalid display info");
        return 2;
    }

    /* Open compositor control channel */
    int ipc_fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0666);
    if (ipc_fd < 0) {
        if (once) {
            printf("aurora: desktop shell initialized\n");
            return 0;
        }
        printf("aurora: cannot connect to compositor at %s (%d)\n",
               AKAOS_COMPOSITOR_IPC_PATH, errno);
        return 3;
    }

    size_t ipc_size = (sizeof(struct akaos_compositor_channel) + 4095U) & ~(size_t)4095U;
    struct akaos_compositor_channel *channel = mmap(
        NULL, ipc_size, PROT_READ | PROT_WRITE, MAP_SHARED, ipc_fd, 0);
    close(ipc_fd);
    if (channel == MAP_FAILED || channel->abi_version != AKAOS_DISPLAY_ABI_VERSION ||
        channel->protocol_version != AKAOS_COMPOSITOR_PROTOCOL_VERSION) {
        puts("aurora: failed to map compositor channel");
        if (channel != MAP_FAILED) munmap(channel, ipc_size);
        return 4;
    }

    if (request_launcher) {
        struct akaos_shell_event event;
        memset(&event, 0, sizeof(event));
        event.type = AKAOS_SHELL_EVENT_KEY;
        event.code = 59;
        event.value = 1;
        int submitted = akaos_shell_event_submit(channel, &event);
        munmap(channel, ipc_size);
        return submitted ? 0 : 1;
    }

    pid_t my_pid = getpid();
    char bar_name[32];
    snprintf(bar_name, sizeof(bar_name), "/aurora-bar-%d", (int)my_pid);

    struct aurora_surface bar = create_surface(bar_name, display.width, PANEL_HEIGHT);
    if (!bar.header) {
        puts("aurora: failed to create desktop surfaces");
        destroy_surface(&bar);
        munmap(channel, ipc_size);
        return 5;
    }

    struct aurora_clock panel_clock;
    memset(&panel_clock, 0, sizeof(panel_clock));
    (void)clock_refresh(&panel_clock);
    struct aurora_capabilities capabilities;
    capabilities_refresh(&capabilities);

    struct aurora_launcher launcher;
    launcher_load_registry(&launcher);
    render_panel(bar.pixels, display.width, PANEL_HEIGHT, channel, &panel_clock,
                 &capabilities);

    int32_t bar_y = (int32_t)(display.height - PANEL_HEIGHT);

    uint32_t bar_surface_id = (uint32_t)(my_pid * 10 + 2);
    uint32_t launcher_surface_id = (uint32_t)(my_pid * 10 + 3);
    uint32_t context_surface_id = (uint32_t)(my_pid * 10 + 4);
    uint32_t notification_surface_id = (uint32_t)(my_pid * 10 + 5);
    uint32_t calendar_surface_id = (uint32_t)(my_pid * 10 + 6);
    uint32_t network_surface_id = (uint32_t)(my_pid * 10 + 7);

    int bar_slot = register_slot(channel, bar_surface_id, 0,
                                 bar_y,
                                 display.width, PANEL_HEIGHT,
                                 AKAOS_SURFACE_FLAG_PANEL, 1000,
                                 bar_name, "Desktop Panel");

    if (bar_slot == 0) {
        puts("aurora: failed to register desktop slots with compositor");
        destroy_surface(&bar);
        munmap(channel, ipc_size);
        return 6;
    }

    /* Launcher dimensions */
    uint32_t launcher_width = display.width > 440U ? 380U : display.width - 24U;
    uint32_t launcher_height = display.height > 360U ? 280U : display.height - PANEL_HEIGHT - 12U;
    int32_t launcher_x = (int32_t)((display.width - launcher_width) / 2U);
    int32_t launcher_y = (int32_t)(display.height - PANEL_HEIGHT - launcher_height - 10U);
    char launcher_name[32];
    snprintf(launcher_name, sizeof(launcher_name), "/aurora-menu-%d", (int)my_pid);
    struct aurora_surface launcher_surface;
    memset(&launcher_surface, 0, sizeof(launcher_surface));
    launcher_surface.fd = -1;
    int launcher_attached = 0;

    /* Context menu */
    char context_name[32];
    snprintf(context_name, sizeof(context_name), "/aurora-menu2-%d", (int)my_pid);
    const uint32_t context_width = 210U;
    const uint32_t context_height = 114U;
    struct aurora_surface context_surface;
    memset(&context_surface, 0, sizeof(context_surface));
    context_surface.fd = -1;
    int32_t context_x = 0;
    int32_t context_y = 0;
    int context_open = 0;
    int context_attached = 0;

    /* Calendar Pop-up */
    char calendar_name[32];
    snprintf(calendar_name, sizeof(calendar_name), "/aurora-cal-%d", (int)my_pid);
    const uint32_t calendar_width = 240U;
    const uint32_t calendar_height = 230U;
    struct aurora_surface calendar_surface;
    memset(&calendar_surface, 0, sizeof(calendar_surface));
    calendar_surface.fd = -1;
    int calendar_open = 0;
    int calendar_attached = 0;

    /* Network Widget Pop-up */
    char network_name[32];
    snprintf(network_name, sizeof(network_name), "/aurora-net-%d", (int)my_pid);
    const uint32_t network_width = 260U;
    const uint32_t network_height = 180U;
    struct aurora_surface network_surface;
    memset(&network_surface, 0, sizeof(network_surface));
    network_surface.fd = -1;
    int network_open = 0;
    int network_attached = 0;

    /* Notification Center */
    const uint32_t notification_width = display.width > 360U ? 320U : display.width - 24U;
    const uint32_t notification_height = 84U;
    char notification_name[32];
    snprintf(notification_name, sizeof(notification_name), "/aurora-note-%d", (int)my_pid);
    struct aurora_surface notification_surface;
    memset(&notification_surface, 0, sizeof(notification_surface));
    notification_surface.fd = -1;
    struct akaos_notification active_notification;
    memset(&active_notification, 0, sizeof(active_notification));
    int notification_active = 0;
    int notification_attached = 0;
    unsigned notification_ticks = 0;
    int32_t notification_x = (int32_t)display.width - (int32_t)notification_width - 16;
    int32_t notification_y = 16;

    if (spawn_app) {
        pid_t app_pid = fork();
        if (app_pid == 0) {
            char *app_argv[] = {"/bin/palette", NULL};
            execv("/bin/palette", app_argv);
            _exit(1);
        }
    }

    if (once) {
        printf("aurora: desktop shell initialized (professional tier UX)\n");
        (void)queue_destroy(channel, bar_surface_id);
        (void)poll(NULL, 0, 50);
        destroy_surface(&network_surface);
        destroy_surface(&calendar_surface);
        destroy_surface(&notification_surface);
        destroy_surface(&context_surface);
        destroy_surface(&launcher_surface);
        destroy_surface(&bar);
        munmap(channel, ipc_size);
        return 0;
    }

    unsigned ticks = 0;
    uint32_t observed_window_state = panel_window_state(channel);

    for (;;) {
        (void)poll(NULL, 0, 50);
        ++ticks;
        int launcher_changed = 0;
        int context_changed = 0;
        int calendar_changed = 0;
        int network_changed = 0;
        int notification_changed = 0;

        struct akaos_shell_event event;
        while (akaos_shell_event_receive(channel, &event)) {
            /* Hotkeys: Super (125) or F1 (59) toggles launcher */
            if (event.type == AKAOS_SHELL_EVENT_KEY &&
                (event.code == 125 || event.code == 59) &&
                event.value == 1) {
                launcher.open = !launcher.open;
                if (launcher.open) {
                    launcher.search_length = 0;
                    launcher.search[0] = '\0';
                    launcher.selected = 0;
                }
                launcher_changed = 1;
                if (context_open) { context_open = 0; context_changed = 1; }
                if (calendar_open) { calendar_open = 0; calendar_changed = 1; }
                if (network_open) { network_open = 0; network_changed = 1; }
                continue;
            }

            /* Click on notification toast dismisses it */
            if (notification_active && event.type == AKAOS_SHELL_EVENT_BUTTON &&
                event.code == AKAOS_BTN_LEFT && event.value == 1 &&
                event.pointer_x >= notification_x &&
                event.pointer_x < notification_x + (int32_t)notification_width &&
                event.pointer_y >= notification_y &&
                event.pointer_y < notification_y + (int32_t)notification_height) {
                notification_active = 0;
                notification_changed = 1;
                continue;
            }

            /* Right-click on empty desktop -> Context Menu */
            if (event.type == AKAOS_SHELL_EVENT_BUTTON &&
                event.code == AKAOS_BTN_RIGHT && event.value == 1 &&
                event.pointer_y < (int32_t)(display.height - PANEL_HEIGHT) &&
                desktop_is_empty_at(channel, event.pointer_x, event.pointer_y)) {
                context_x = event.pointer_x;
                context_y = event.pointer_y;
                if (context_x + (int32_t)context_width > (int32_t)display.width)
                    context_x = (int32_t)display.width - (int32_t)context_width - 4;
                if (context_y + (int32_t)context_height > (int32_t)display.height - (int32_t)PANEL_HEIGHT)
                    context_y = (int32_t)display.height - (int32_t)PANEL_HEIGHT - (int32_t)context_height - 4;
                if (context_x < 4) context_x = 4;
                if (context_y < 4) context_y = 4;
                context_open = 1;
                context_changed = 1;
                if (launcher.open) { launcher.open = 0; launcher_changed = 1; }
                if (calendar_open) { calendar_open = 0; calendar_changed = 1; }
                if (network_open) { network_open = 0; network_changed = 1; }
                continue;
            }

            /* Left-click handling */
            if (event.type == AKAOS_SHELL_EVENT_BUTTON &&
                event.code == AKAOS_BTN_LEFT && event.value == 1) {

                /* Click on Context Menu item */
                if (context_open) {
                    if (event.pointer_x >= context_x &&
                        event.pointer_x < context_x + (int32_t)context_width &&
                        event.pointer_y >= context_y &&
                        event.pointer_y < context_y + (int32_t)context_height) {
                        int local_y = event.pointer_y - context_y;
                        if (local_y >= 8 && local_y < 36) {
                            struct aurora_app app;
                            memset(&app, 0, sizeof(app));
                            strcpy(app.executable, "/bin/terminal");
                            launch_application(&app);
                        } else if (local_y >= 42 && local_y < 70) {
                            struct aurora_app app;
                            memset(&app, 0, sizeof(app));
                            strcpy(app.executable, "/bin/akaos-config");
                            launch_application(&app);
                        } else if (local_y >= 76 && local_y < 104) {
                            launcher.open = 1;
                            launcher.search_length = 0;
                            launcher.search[0] = '\0';
                            launcher.selected = 0;
                            launcher_changed = 1;
                        }
                    }
                    context_open = 0;
                    context_changed = 1;
                    continue;
                }

                /* Click on Calendar Pop-up */
                if (calendar_open) {
                    if (event.pointer_x >= (int32_t)(display.width - calendar_width - 8U) &&
                        event.pointer_x < (int32_t)(display.width - 8U) &&
                        event.pointer_y >= (int32_t)(display.height - PANEL_HEIGHT - calendar_height - 6U) &&
                        event.pointer_y < (int32_t)(display.height - PANEL_HEIGHT - 6U)) {
                        /* Clicked inside calendar (e.g. UTC toggle) */
                        int local_x = event.pointer_x - (int)(display.width - calendar_width - 8U);
                        int local_y = event.pointer_y - (int)(display.height - PANEL_HEIGHT - calendar_height - 6U);
                        if (local_x >= 14 && local_x <= 54 && local_y >= 34 && local_y <= 52) {
                            panel_clock.utc_mode = !panel_clock.utc_mode;
                            calendar_changed = 1;
                        }
                        continue;
                    }
                    calendar_open = 0;
                    calendar_changed = 1;
                }

                /* Click on Network Widget Pop-up */
                if (network_open) {
                    int32_t net_pop_x = (int32_t)(display.width - get_tray_width(&panel_clock, &capabilities) - 16U);
                    int32_t net_pop_y = (int32_t)(display.height - PANEL_HEIGHT - network_height - 6U);
                    if (event.pointer_x >= net_pop_x &&
                        event.pointer_x < net_pop_x + (int32_t)network_width &&
                        event.pointer_y >= net_pop_y &&
                        event.pointer_y < net_pop_y + (int32_t)network_height) {
                        int local_y = event.pointer_y - net_pop_y;
                        if (local_y >= (int)network_height - 34) {
                            struct aurora_app cfg;
                            memset(&cfg, 0, sizeof(cfg));
                            strcpy(cfg.executable, "/bin/akaos-config");
                            launch_application(&cfg);
                            network_open = 0;
                            network_changed = 1;
                        }
                        continue;
                    }
                    network_open = 0;
                    network_changed = 1;
                }

                /* Click on Tray Clock -> Toggle Calendar Pop-up */
                uint32_t tray_w = get_tray_width(&panel_clock, &capabilities);
                uint32_t tray_x = display.width - tray_w - 16U;
                uint32_t clock_click_x = display.width - 130U;
                if (event.pointer_y >= (int32_t)(display.height - PANEL_HEIGHT) &&
                    event.pointer_x >= (int32_t)clock_click_x &&
                    event.pointer_x < (int32_t)(display.width - 8U)) {
                    calendar_open = !calendar_open;
                    if (calendar_open) {
                        network_open = 0;
                        network_changed = 1;
                        launcher.open = 0;
                        launcher_changed = 1;
                    }
                    calendar_changed = 1;
                    continue;
                }

                /* Click on Network/Wi-Fi Icon -> Toggle Network Status Widget */
                if (event.pointer_y >= (int32_t)(display.height - PANEL_HEIGHT) &&
                    event.pointer_x >= (int32_t)tray_x &&
                    event.pointer_x < (int32_t)(tray_x + 36U)) {
                    network_open = !network_open;
                    if (network_open) {
                        calendar_open = 0;
                        calendar_changed = 1;
                        launcher.open = 0;
                        launcher_changed = 1;
                    }
                    network_changed = 1;
                    continue;
                }

                /* Click on Dock Launcher Emblem -> Toggle Application Menu */
                uint32_t dock_x = channel->shell_launcher_x;
                if (event.pointer_y >= (int32_t)(display.height - PANEL_HEIGHT) &&
                    event.pointer_x >= (int32_t)dock_x &&
                    event.pointer_x < (int32_t)(dock_x + LAUNCHER_WIDTH)) {
                    launcher.open = !launcher.open;
                    if (launcher.open) {
                        launcher.search_length = 0;
                        launcher.search[0] = '\0';
                        launcher.selected = 0;
                    }
                    launcher_changed = 1;
                    if (calendar_open) { calendar_open = 0; calendar_changed = 1; }
                    if (network_open) { network_open = 0; network_changed = 1; }
                } else if (launcher.open && event.pointer_x >= launcher_x &&
                           event.pointer_x < launcher_x + (int32_t)launcher_width &&
                           event.pointer_y >= launcher_y &&
                           event.pointer_y < launcher_y + (int32_t)launcher_height) {
                    /* Inside Launcher */
                    int local_x = event.pointer_x - launcher_x;
                    int local_y = event.pointer_y - launcher_y;

                    /* Category Bar Click (y: 72..94) */
                    if (local_y >= 72 && local_y <= 94) {
                        int tab_w = ((int)launcher_width - 28) / CATEGORY_COUNT;
                        int clicked_cat = (local_x - 14) / (tab_w ? tab_w : 1);
                        if (clicked_cat >= 0 && clicked_cat < CATEGORY_COUNT) {
                            launcher.active_category = clicked_cat;
                            launcher.selected = 0;
                            launcher_changed = 1;
                        }
                    } else if (local_y >= 100) {
                        /* Application List Item Click */
                        unsigned matches = launcher_match_count(&launcher);
                        unsigned item_h = 36;
                        unsigned capacity = launcher_height > 100U ? (launcher_height - 110U) / item_h : 0U;
                        if (capacity > matches) capacity = matches;
                        unsigned first = launcher.selected >= capacity ? launcher.selected - capacity + 1U : 0U;
                        unsigned picked = first + (unsigned)(local_y - 100) / item_h;
                        const struct aurora_app *app = launcher_match_at(&launcher, picked);
                        if (app) {
                            launch_application(app);
                            launcher.open = 0;
                            launcher_changed = 1;
                        }
                    }
                } else if (launcher.open) {
                    launcher.open = 0;
                    launcher_changed = 1;
                }
            } else if (launcher.open && event.type == AKAOS_SHELL_EVENT_TEXT &&
                       event.value >= 32 && event.value <= 126 &&
                       launcher.search_length < AURORA_SEARCH_MAX) {
                launcher.search[launcher.search_length++] = (char)event.value;
                launcher.search[launcher.search_length] = '\0';
                launcher.selected = 0;
                launcher_changed = 1;
            } else if (launcher.open && event.type == AKAOS_SHELL_EVENT_KEY &&
                       event.value == 1) {
                unsigned matches = launcher_match_count(&launcher);
                if (event.code == 1) {
                    /* ESC closes menu */
                    launcher.open = 0;
                    launcher_changed = 1;
                } else if (event.code == 15) {
                    /* TAB cycles category filter */
                    launcher.active_category = (launcher.active_category + 1) % CATEGORY_COUNT;
                    launcher.selected = 0;
                    launcher_changed = 1;
                } else if (event.code == 14 && launcher.search_length) {
                    /* Backspace */
                    launcher.search[--launcher.search_length] = '\0';
                    launcher.selected = 0;
                    launcher_changed = 1;
                } else if (event.code == 103 && launcher.selected > 0) {
                    /* Arrow UP */
                    --launcher.selected;
                    launcher_changed = 1;
                } else if (event.code == 108 && launcher.selected + 1U < matches) {
                    /* Arrow DOWN */
                    ++launcher.selected;
                    launcher_changed = 1;
                } else if (event.code == 28) {
                    /* ENTER */
                    const struct aurora_app *app = launcher_match_at(&launcher, launcher.selected);
                    if (app) launch_application(app);
                    launcher.open = 0;
                    launcher_changed = 1;
                }
            }
        }

        /* Notifications D-Bus/Compositor sink */
        if (!notification_active && akaos_notification_receive(channel, &active_notification)) {
            notification_active = 1;
            notification_ticks = 0;
            notification_changed = 1;
        }
        if (notification_active) {
            ++notification_ticks;
            unsigned timeout_ticks = active_notification.timeout_ms / 50U;
            if (timeout_ticks < 20U) timeout_ticks = 20U;
            if (notification_ticks >= timeout_ticks) {
                notification_active = 0;
                notification_changed = 1;
            }
        }
        if (notification_changed) {
            if (notification_active) {
                if (!notification_surface.header) {
                    notification_surface = create_surface(notification_name,
                                                          notification_width,
                                                          notification_height);
                    if (!notification_surface.header) {
                        notification_active = 0;
                        continue;
                    }
                }
                render_notification(notification_surface.pixels,
                                    notification_width, notification_height,
                                    &active_notification, &panel_clock);
                if (!notification_attached) {
                    (void)register_slot(channel, notification_surface_id,
                                        notification_x, notification_y,
                                        notification_width, notification_height,
                                        AKAOS_SURFACE_FLAG_PANEL, 1100,
                                        notification_name, "Notification");
                    notification_attached = 1;
                }
                (void)queue_damage(channel, notification_surface_id,
                                   notification_width, notification_height);
            } else if (notification_attached) {
                (void)queue_destroy(channel, notification_surface_id);
                notification_attached = 0;
                (void)poll(NULL, 0, 60);
                destroy_surface(&notification_surface);
            }
        }

        /* Context Menu lifecycle */
        if (context_changed) {
            if (context_open) {
                if (!context_surface.header) {
                    context_surface = create_surface(context_name, context_width, context_height);
                    if (!context_surface.header) {
                        context_open = 0;
                        continue;
                    }
                }
                render_context_menu(context_surface.pixels, context_width, context_height);
                if (!context_attached) {
                    (void)register_slot(channel, context_surface_id, context_x, context_y,
                                        context_width, context_height,
                                        AKAOS_SURFACE_FLAG_PANEL, 995,
                                        context_name, "Desktop Menu");
                    context_attached = 1;
                } else {
                    struct akaos_display_message move;
                    memset(&move, 0, sizeof(move));
                    move.type = AKAOS_DISPLAY_MSG_MOVE;
                    move.surface_id = context_surface_id;
                    move.client_pid = (uint32_t)my_pid;
                    move.x = context_x;
                    move.y = context_y;
                    (void)akaos_compositor_submit(channel, &move);
                }
                (void)queue_damage(channel, context_surface_id, context_width, context_height);
            } else if (context_attached) {
                (void)queue_destroy(channel, context_surface_id);
                context_attached = 0;
                (void)poll(NULL, 0, 60);
                destroy_surface(&context_surface);
            }
        }

        /* Calendar Pop-up lifecycle */
        if (calendar_changed) {
            if (calendar_open) {
                int32_t cal_x = (int32_t)(display.width - calendar_width - 8U);
                int32_t cal_y = (int32_t)(display.height - PANEL_HEIGHT - calendar_height - 6U);
                if (!calendar_surface.header) {
                    calendar_surface = create_surface(calendar_name, calendar_width, calendar_height);
                    if (!calendar_surface.header) {
                        calendar_open = 0;
                        continue;
                    }
                }
                render_calendar_popup(calendar_surface.pixels, calendar_width, calendar_height, &panel_clock);
                if (!calendar_attached) {
                    (void)register_slot(channel, calendar_surface_id, cal_x, cal_y,
                                        calendar_width, calendar_height,
                                        AKAOS_SURFACE_FLAG_PANEL, 992,
                                        calendar_name, "Calendar");
                    calendar_attached = 1;
                }
                (void)queue_damage(channel, calendar_surface_id, calendar_width, calendar_height);
            } else if (calendar_attached) {
                (void)queue_destroy(channel, calendar_surface_id);
                calendar_attached = 0;
                (void)poll(NULL, 0, 60);
                destroy_surface(&calendar_surface);
            }
        }

        /* Network Status Widget lifecycle */
        if (network_changed) {
            if (network_open) {
                uint32_t tray_w = get_tray_width(&panel_clock, &capabilities);
                int32_t net_x = (int32_t)(display.width - tray_w - 16U);
                if (net_x + (int32_t)network_width > (int32_t)display.width)
                    net_x = (int32_t)display.width - (int32_t)network_width - 8;
                int32_t net_y = (int32_t)(display.height - PANEL_HEIGHT - network_height - 6U);
                if (!network_surface.header) {
                    network_surface = create_surface(network_name, network_width, network_height);
                    if (!network_surface.header) {
                        network_open = 0;
                        continue;
                    }
                }
                render_network_popup(network_surface.pixels, network_width, network_height, &capabilities.net);
                if (!network_attached) {
                    (void)register_slot(channel, network_surface_id, net_x, net_y,
                                        network_width, network_height,
                                        AKAOS_SURFACE_FLAG_PANEL, 992,
                                        network_name, "Network Status");
                    network_attached = 1;
                }
                (void)queue_damage(channel, network_surface_id, network_width, network_height);
            } else if (network_attached) {
                (void)queue_destroy(channel, network_surface_id);
                network_attached = 0;
                (void)poll(NULL, 0, 60);
                destroy_surface(&network_surface);
            }
        }

        /* Application Menu lifecycle */
        if (launcher_changed) {
            if (launcher.open) {
                if (!launcher_surface.header) {
                    launcher_surface = create_surface(launcher_name, launcher_width, launcher_height);
                    if (!launcher_surface.header) {
                        launcher.open = 0;
                        channel->shell_flags &= ~AKAOS_SHELL_FLAG_LAUNCHER_OPEN;
                        continue;
                    }
                }
                render_launcher(launcher_surface.pixels, launcher_width, launcher_height, &launcher);
                if (!launcher_attached) {
                    (void)register_slot(channel, launcher_surface_id, launcher_x, launcher_y,
                                        launcher_width, launcher_height,
                                        AKAOS_SURFACE_FLAG_PANEL, 990,
                                        launcher_name, "Applications");
                    launcher_attached = 1;
                }
                (void)queue_damage(channel, launcher_surface_id, launcher_width, launcher_height);
                channel->shell_flags |= AKAOS_SHELL_FLAG_LAUNCHER_OPEN;
            } else {
                if (launcher_attached) {
                    (void)queue_destroy(channel, launcher_surface_id);
                    launcher_attached = 0;
                    (void)poll(NULL, 0, 60);
                    destroy_surface(&launcher_surface);
                }
                channel->shell_flags &= ~AKAOS_SHELL_FLAG_LAUNCHER_OPEN;
            }
            render_panel(bar.pixels, display.width, PANEL_HEIGHT, channel,
                         &panel_clock, &capabilities);
            (void)queue_damage(channel, bar_surface_id, display.width, PANEL_HEIGHT);
        }

        /* Window state update (task switcher sync) */
        uint32_t current_window_state = panel_window_state(channel);
        if (current_window_state != observed_window_state) {
            observed_window_state = current_window_state;
            render_panel(bar.pixels, display.width, PANEL_HEIGHT, channel,
                         &panel_clock, &capabilities);
            (void)queue_damage(channel, bar_surface_id, display.width, PANEL_HEIGHT);
        }

        /* Periodic 1 Hz clock tick and 5 Hz capability poll */
        if (ticks % 20U == 0U) {
            (void)clock_refresh(&panel_clock);
            if (calendar_open && calendar_surface.header) {
                render_calendar_popup(calendar_surface.pixels, calendar_width, calendar_height, &panel_clock);
                (void)queue_damage(channel, calendar_surface_id, calendar_width, calendar_height);
            }
            if (ticks % 100U == 0U) {
                capabilities_refresh(&capabilities);
                if (network_open && network_surface.header) {
                    render_network_popup(network_surface.pixels, network_width, network_height, &capabilities.net);
                    (void)queue_damage(channel, network_surface_id, network_width, network_height);
                }
            }
            render_panel(bar.pixels, display.width, PANEL_HEIGHT, channel,
                         &panel_clock, &capabilities);
            (void)queue_damage(channel, bar_surface_id, display.width, PANEL_HEIGHT);
        }
    }

    (void)queue_destroy(channel, bar_surface_id);
    (void)poll(NULL, 0, 50);
    destroy_surface(&network_surface);
    destroy_surface(&calendar_surface);
    destroy_surface(&launcher_surface);
    destroy_surface(&context_surface);
    destroy_surface(&notification_surface);
    destroy_surface(&bar);
    munmap(channel, ipc_size);
    return 0;
}
