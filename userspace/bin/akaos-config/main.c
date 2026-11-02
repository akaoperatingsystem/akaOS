#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONFIG_PATH "/etc/akaos.conf"

struct akaos_config {
    char hostname[64];
    int httpd_enabled;
    int httpd_port;
    int telnetd_enabled;
    int telnetd_port;
    int tftpd_enabled;
    int tftpd_port;
    int sshd_enabled;
    int sshd_port;
    int audio_bell;
    int autostart_desktop;
    char desktop_env[32];
    char desktop_theme[8];
};

struct desktop_env_entry {
    char id[16];
    char name[24];
    char desc[48];
    char bin_path[32];
};

static const struct desktop_env_entry KNOWN_DESKTOPS[] = {
    {"aurora", "Aurora", "akaOS Native Desktop Environment", "/bin/aurora"},
    {"xfce", "XFCE", "XFCE 4.18 Desktop Environment", "/bin/xfce4-session"},
    {"lxde", "LXDE", "GTK/Openbox Desktop Suite", "/bin/startlxde"},
    {"openbox", "Openbox", "Standalone Openbox Window Manager", "/bin/openbox"},
    {"mate", "MATE", "MATE Desktop Environment", "/bin/mate-session"},
    {"lxqt", "LXQt", "Qt/Openbox Desktop Environment", "/bin/startlxqt"},
    {"kde", "KDE Plasma", "KDE Plasma Desktop Shell", "/bin/plasma"},
    {"gnome", "GNOME Shell", "GNOME Shell Desktop", "/bin/gnome-shell"},
    {"none", "None", "Pure Console TTY (No GUI Desktop)", "/bin/akash"},
};

static int is_desktop_installed(const char *bin_path) {
    if (!bin_path) return 0;
    int fd = open(bin_path, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return 1;
    }
    return 0;
}

static const char *resolve_desktop_path(const char *id) {
    if (!id) return NULL;
    if (strcmp(id, "aurora") == 0) return "/bin/aurora";
    if (strcmp(id, "xfce") == 0) return "/bin/xfce4-session";
    if (strcmp(id, "lxde") == 0) return "/bin/startlxde";
    if (strcmp(id, "openbox") == 0) return "/bin/openbox";
    if (strcmp(id, "mate") == 0) return "/bin/mate-session";
    if (strcmp(id, "lxqt") == 0) return "/bin/startlxqt";
    if (strcmp(id, "kde") == 0) return "/bin/plasma";
    if (strcmp(id, "gnome") == 0) return "/bin/gnome-shell";
    if (strcmp(id, "none") == 0) return "/bin/akash";
    return NULL;
}

static int valid_port(int port) {
    return port >= 1 && port <= 65535;
}

static int parse_port(const char *text, int *result) {
    if (!text || !*text || !result) return 0;
    unsigned int value = 0;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (text[i] < '0' || text[i] > '9') return 0;
        unsigned int digit = (unsigned int)(text[i] - '0');
        if (value > 6553U || (value == 6553U && digit > 5U)) return 0;
        value = value * 10U + digit;
    }
    if (value == 0) return 0;
    *result = (int)value;
    return 1;
}

static int parse_toggle(const char *text, int *result) {
    if (!text || !result || (text[0] != '0' && text[0] != '1') || text[1] != '\0') return 0;
    *result = text[0] == '1';
    return 1;
}

static int valid_hostname(const char *hostname) {
    if (!hostname || !*hostname) return 0;
    size_t length = strlen(hostname);
    if (length > 63 || hostname[0] == '.' || hostname[0] == '-' ||
        hostname[length - 1] == '.' || hostname[length - 1] == '-') return 0;
    for (size_t i = 0; i < length; ++i) {
        char ch = hostname[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '.')) return 0;
    }
    return 1;
}

static void normalize_config(struct akaos_config *cfg) {
    if (!cfg) return;
    if (!valid_hostname(cfg->hostname)) {
        strncpy(cfg->hostname, "akaOS", sizeof(cfg->hostname) - 1);
        cfg->hostname[sizeof(cfg->hostname) - 1] = '\0';
    }
    if (!valid_port(cfg->httpd_port)) cfg->httpd_port = 80;
    if (!valid_port(cfg->telnetd_port)) cfg->telnetd_port = 23;
    if (!valid_port(cfg->tftpd_port)) cfg->tftpd_port = 69;
    if (!valid_port(cfg->sshd_port)) cfg->sshd_port = 22;
    cfg->httpd_enabled = cfg->httpd_enabled ? 1 : 0;
    cfg->telnetd_enabled = cfg->telnetd_enabled ? 1 : 0;
    cfg->tftpd_enabled = cfg->tftpd_enabled ? 1 : 0;
    cfg->sshd_enabled = cfg->sshd_enabled ? 1 : 0;
    cfg->audio_bell = cfg->audio_bell ? 1 : 0;
    cfg->autostart_desktop = cfg->autostart_desktop ? 1 : 0;
    if (!resolve_desktop_path(cfg->desktop_env)) {
        strncpy(cfg->desktop_env, "aurora", sizeof(cfg->desktop_env) - 1);
        cfg->desktop_env[sizeof(cfg->desktop_env) - 1] = '\0';
    }
    if (strcmp(cfg->desktop_theme, "light") != 0 &&
        strcmp(cfg->desktop_theme, "dark") != 0)
        strcpy(cfg->desktop_theme, "dark");
}

static void init_default_config(struct akaos_config *cfg) {
    strncpy(cfg->hostname, "akaOS", sizeof(cfg->hostname) - 1);
    cfg->hostname[sizeof(cfg->hostname) - 1] = '\0';
    cfg->httpd_enabled = 0;
    cfg->httpd_port = 80;
    cfg->telnetd_enabled = 0;
    cfg->telnetd_port = 23;
    cfg->tftpd_enabled = 0;
    cfg->tftpd_port = 69;
    cfg->sshd_enabled = 0;
    cfg->sshd_port = 22;
    cfg->audio_bell = 1;
    cfg->autostart_desktop = 0;
    strncpy(cfg->desktop_env, "aurora", sizeof(cfg->desktop_env) - 1);
    cfg->desktop_env[sizeof(cfg->desktop_env) - 1] = '\0';
    strcpy(cfg->desktop_theme, "dark");
}

static __attribute__((noinline)) int load_config(struct akaos_config *cfg) {
    init_default_config(cfg);
    int fd = open(CONFIG_PATH, O_RDONLY);
    if (fd < 0) return 0;

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    char *line = buf;
    while (*line) {
        if (strncmp(line, "HOSTNAME=", 9) == 0) {
            char *val = line + 9;
            int idx = 0;
            while (*val && *val != '\r' && *val != '\n' && idx < (int)sizeof(cfg->hostname) - 1) {
                cfg->hostname[idx++] = *val++;
            }
            cfg->hostname[idx] = '\0';
        } else if (strncmp(line, "HTTPD_ENABLED=", 14) == 0) {
            cfg->httpd_enabled = (line[14] == '1');
        } else if (strncmp(line, "HTTPD_PORT=", 11) == 0) {
            cfg->httpd_port = atoi(line + 11);
        } else if (strncmp(line, "TELNETD_ENABLED=", 16) == 0) {
            cfg->telnetd_enabled = (line[16] == '1');
        } else if (strncmp(line, "TELNETD_PORT=", 13) == 0) {
            cfg->telnetd_port = atoi(line + 13);
        } else if (strncmp(line, "TFTPD_ENABLED=", 14) == 0) {
            cfg->tftpd_enabled = (line[14] == '1');
        } else if (strncmp(line, "TFTPD_PORT=", 11) == 0) {
            cfg->tftpd_port = atoi(line + 11);
        } else if (strncmp(line, "SSHD_ENABLED=", 13) == 0) {
            cfg->sshd_enabled = (line[13] == '1');
        } else if (strncmp(line, "SSHD_PORT=", 10) == 0) {
            cfg->sshd_port = atoi(line + 10);
        } else if (strncmp(line, "AUDIO_BELL=", 11) == 0) {
            cfg->audio_bell = (line[11] == '1');
        } else if (strncmp(line, "AUTOSTART_DESKTOP=", 18) == 0) {
            cfg->autostart_desktop = (line[18] == '1');
        } else if (strncmp(line, "DESKTOP_ENV=", 12) == 0) {
            char *val = line + 12;
            int idx = 0;
            while (*val && *val != '\r' && *val != '\n' && idx < (int)sizeof(cfg->desktop_env) - 1) {
                cfg->desktop_env[idx++] = *val++;
            }
            cfg->desktop_env[idx] = '\0';
        } else if (strncmp(line, "DESKTOP_THEME=", 14) == 0) {
            char *val = line + 14;
            int idx = 0;
            while (*val && *val != '\r' && *val != '\n' &&
                   idx < (int)sizeof(cfg->desktop_theme) - 1)
                cfg->desktop_theme[idx++] = *val++;
            cfg->desktop_theme[idx] = '\0';
        }
        char *nl = strchr(line, '\n');
        if (!nl) break;
        line = nl + 1;
    }
    normalize_config(cfg);
    return 1;
}

static __attribute__((noinline)) int save_config(const struct akaos_config *cfg) {
    int fd = open(CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 0;

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "# akaOS System Configuration\n"
             "HOSTNAME=%s\n"
             "HTTPD_ENABLED=%d\n"
             "HTTPD_PORT=%d\n"
             "TELNETD_ENABLED=%d\n"
             "TELNETD_PORT=%d\n"
             "TFTPD_ENABLED=%d\n"
             "TFTPD_PORT=%d\n"
             "SSHD_ENABLED=%d\n"
             "SSHD_PORT=%d\n"
             "AUDIO_BELL=%d\n"
             "AUTOSTART_DESKTOP=%d\n"
             "DESKTOP_ENV=%s\n"
             "DESKTOP_THEME=%s\n",
             cfg->hostname,
             cfg->httpd_enabled ? 1 : 0,
             cfg->httpd_port,
             cfg->telnetd_enabled ? 1 : 0,
             cfg->telnetd_port,
             cfg->tftpd_enabled ? 1 : 0,
             cfg->tftpd_port,
             cfg->sshd_enabled ? 1 : 0,
             cfg->sshd_port,
             cfg->audio_bell ? 1 : 0,
             cfg->autostart_desktop ? 1 : 0,
             cfg->desktop_env,
             cfg->desktop_theme);

    size_t length = strlen(buf);
    size_t written = 0;
    while (written < length) {
        ssize_t result = write(fd, buf + written, length - written);
        if (result <= 0) {
            close(fd);
            return 0;
        }
        written += (size_t)result;
    }
    return close(fd) == 0;
}

static int commit_config(const struct akaos_config *cfg) {
    if (save_config(cfg)) return 1;
    fprintf(2, "akaos-config: error: unable to persist %s\n", CONFIG_PATH);
    return 0;
}

static void print_usage(void) {
    puts("Usage: akaos-config [options]\n"
         "akaOS System Configuration Tool (similar to raspi-config)\n\n"
         "Options:\n"
         "  --status                     Display current system configuration\n"
         "  --enable-httpd               Enable micro HTTP web server\n"
         "  --disable-httpd              Disable micro HTTP web server\n"
         "  --port <port>                Set micro HTTP server port\n"
         "  --enable-telnetd             Enable remote Telnet daemon\n"
         "  --disable-telnetd            Disable remote Telnet daemon\n"
         "  --telnet-port <port>         Set Telnet daemon port\n"
         "  --enable-tftpd               Enable TFTP file daemon\n"
         "  --disable-tftpd              Disable TFTP file daemon\n"
         "  --tftp-port <port>           Set TFTP daemon port\n"
         "  --enable-sshd                Enable secure shell daemon (sshd)\n"
         "  --disable-sshd               Disable secure shell daemon (sshd)\n"
         "  --sshd-port <port>           Set secure shell daemon port\n"
         "  --autostart-desktop <0|1>    Set desktop autostart on login (0=off, 1=on)\n"
         "  --enable-autostart-desktop   Enable desktop autostart on login\n"
         "  --disable-autostart-desktop  Disable desktop autostart on login\n"
         "  --desktop-env <env>          Set installed desktop environment (default: aurora)\n"
         "  --theme <dark|light>         Set the Aurora desktop theme\n"
         "  --hostname <name>            Set system hostname\n"
         "  -h, --help                   Display this help and exit\n"
         "  -v, --version                Output version information and exit\n\n"
         "Run without arguments for interactive raspi-config style configuration menu.");
}

static __attribute__((noinline)) void print_status(const struct akaos_config *cfg) {
    puts("========================================");
    puts("       akaOS System Configuration       ");
    puts("========================================");
    printf("Hostname:          %s\n", cfg->hostname);
    printf("Web Server:        %s (Port %d)\n", cfg->httpd_enabled ? "ENABLED [Active]" : "DISABLED [Inactive]", cfg->httpd_port);
    printf("Telnet Server:     %s (Port %d)\n", cfg->telnetd_enabled ? "ENABLED [Active]" : "DISABLED [Inactive]", cfg->telnetd_port);
    printf("TFTP Server:       %s (Port %d)\n", cfg->tftpd_enabled ? "ENABLED [Active]" : "DISABLED [Inactive]", cfg->tftpd_port);
    printf("SSH Server:        %s (Port %d)\n", cfg->sshd_enabled ? "ENABLED [Active]" : "DISABLED [Inactive]", cfg->sshd_port);
    printf("Desktop Autostart: %s\n", cfg->autostart_desktop ? "ENABLED" : "DISABLED");
    const char *path = resolve_desktop_path(cfg->desktop_env);
    int installed = is_desktop_installed(path);
    printf("Desktop Env:       %s%s\n", cfg->desktop_env, installed ? "" : " [Not Installed]");
    printf("Desktop Theme:     %s\n", cfg->desktop_theme);
    printf("Audio Bell:        %s\n", cfg->audio_bell ? "ENABLED" : "DISABLED");
    printf("Config File:       %s\n", CONFIG_PATH);
    puts("========================================");
}

static void read_line(char *buf, size_t size) {
    size_t idx = 0;
    while (idx < size - 1) {
        char ch = '\0';
        if (read(STDIN_FILENO, &ch, 1) <= 0) break;
        if (ch == '\n' || ch == '\r') break;
        buf[idx++] = ch;
    }
    buf[idx] = '\0';
}

/* --- Authentic raspi-config ANSI Terminal UI Engine --- */

#define ANSI_RESET        "\033[0m"
#define ANSI_BLUE_BG      "\033[44m"
#define ANSI_WHITE_FG     "\033[37;1m"
#define ANSI_GRAY_BG      "\033[47m"
#define ANSI_BLACK_FG     "\033[30m"
#define ANSI_RED_BG       "\033[41m"
#define ANSI_HIGHLIGHT_BG "\033[44;37;1m"
#define ANSI_CLEAR_SCREEN "\033[2J\033[H"

static void draw_box_header(const char *title, const char *subtitle) {
    /* Full-screen royal blue background with elegant top banner */
    printf("%s%s%s", ANSI_RESET, ANSI_BLUE_BG, ANSI_CLEAR_SCREEN);
    printf("%s%s  akaOS System & Hardware Configuration Utility (akaos-config) %s%s\n",
           ANSI_BLUE_BG, ANSI_WHITE_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("%s%s  ========================================================================%s%s\n\n",
           ANSI_BLUE_BG, ANSI_WHITE_FG, ANSI_RESET, ANSI_BLUE_BG);

    /* Centered light-grey window with red title badge */
    printf("   %s%s+------------------------------------------------------------------+%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|  %s %-58s %s%s%s  |%s  %s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RED_BG, title, ANSI_RESET, ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|                                                                  |%s  %s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    if (subtitle && *subtitle) {
        printf("   %s%s|  %-64s|%s  %s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, subtitle, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|                                                                  |%s  %s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    }
}

static void draw_box_footer(const char *button1, const char *button2) {
    printf("   %s%s|                                                                  |%s  %s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    char btn_line[80];
    if (button2 && *button2) {
        snprintf(btn_line, sizeof(btn_line), "            < %s >                   < %s >            ",
                 button1, button2);
    } else {
        snprintf(btn_line, sizeof(btn_line), "                          < %s >                          ",
                 button1);
    }
    printf("   %s%s|%-66s|%s  %s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, btn_line, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s+------------------------------------------------------------------+%s  %s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("     %s                                                                    %s\n",
           ANSI_BLUE_BG, ANSI_RESET);
    printf("%s", ANSI_RESET);
}

/* --- Submenu: Desktop & Boot Options --- */

static __attribute__((noinline)) void configure_desktop(struct akaos_config *cfg) {
    for (;;) {
        draw_box_header("Desktop / Boot Options", "Select a desktop option to configure:");
        printf("   %s%s|   1 Autostart Desktop   Launch GUI automatically on login [%-8s] |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, cfg->autostart_desktop ? "ENABLED" : "DISABLED", ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   2 Desktop Environment Choose environment (Current: %-12s)   |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, cfg->desktop_env, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   3 Desktop Theme       Visual style (Current: %-12s)         |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, cfg->desktop_theme, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   4 Return to Main Menu                                          |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        draw_box_footer("Select", "Back");

        printf("\nSelect option [1-4] or 'q' to return: ");
        char choice[16];
        read_line(choice, sizeof(choice));

        if (choice[0] == '1') {
            /* Toggle or choose autostart */
            draw_box_header("Autostart Desktop Configuration", "Should akaOS start the desktop GUI automatically upon login?");
            printf("   %s%s|   1 Enable Autostart Desktop GUI                                 |%s%s\n",
                   ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
            printf("   %s%s|   2 Disable Autostart Desktop GUI (Console Only)                 |%s%s\n",
                   ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
            printf("   %s%s|   3 Cancel                                                       |%s%s\n",
                   ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
            draw_box_footer("Select", "Back");

            printf("\nSelect option [1-3]: ");
            char auto_choice[16];
            read_line(auto_choice, sizeof(auto_choice));
            if (auto_choice[0] == '1') {
                cfg->autostart_desktop = 1;
                (void)commit_config(cfg);
            } else if (auto_choice[0] == '2') {
                cfg->autostart_desktop = 0;
                (void)commit_config(cfg);
            }
        } else if (choice[0] == '2') {
            /* Select Desktop Environment: Only show installed environments */
            const struct desktop_env_entry *installed[8];
            int count = 0;
            for (size_t i = 0; i < sizeof(KNOWN_DESKTOPS) / sizeof(KNOWN_DESKTOPS[0]); ++i) {
                if (is_desktop_installed(KNOWN_DESKTOPS[i].bin_path)) {
                    installed[count++] = &KNOWN_DESKTOPS[i];
                }
            }

            draw_box_header("Choose Desktop Environment", "Installed desktop environments found on system:");
            if (count == 0) {
                printf("   %s%s|   No desktop environments are currently installed in /bin!     |%s%s\n",
                       ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
                printf("   %s%s|   1 Cancel                                                       |%s%s\n",
                       ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
                draw_box_footer("Back", NULL);
                printf("\nPress Enter to return: ");
                char env_choice[16];
                read_line(env_choice, sizeof(env_choice));
            } else {
                for (int i = 0; i < count; ++i) {
                    char line_buf[65];
                    snprintf(line_buf, sizeof(line_buf), "%d %-10s %-48s",
                             i + 1, installed[i]->name, installed[i]->desc);
                    printf("   %s%s|   %-63s|%s%s\n",
                           ANSI_GRAY_BG, ANSI_BLACK_FG, line_buf, ANSI_RESET, ANSI_BLUE_BG);
                }
                char cancel_buf[65];
                snprintf(cancel_buf, sizeof(cancel_buf), "%d Cancel", count + 1);
                printf("   %s%s|   %-63s|%s%s\n",
                       ANSI_GRAY_BG, ANSI_BLACK_FG, cancel_buf, ANSI_RESET, ANSI_BLUE_BG);
                draw_box_footer("Select", "Back");

                printf("\nSelect environment [1-%d]: ", count + 1);
                char env_choice[16];
                read_line(env_choice, sizeof(env_choice));
                int sel = atoi(env_choice);
                if (sel >= 1 && sel <= count) {
                    strncpy(cfg->desktop_env, installed[sel - 1]->id, sizeof(cfg->desktop_env) - 1);
                    cfg->desktop_env[sizeof(cfg->desktop_env) - 1] = '\0';
                    (void)commit_config(cfg);
                }
            }
        } else if (choice[0] == '3') {
            draw_box_header("Aurora Desktop Theme", "Choose a theme for the next desktop session:");
            printf("   %s%s|   1 Dark                                                         |%s%s\n",
                   ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
            printf("   %s%s|   2 Light                                                        |%s%s\n",
                   ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
            printf("   %s%s|   3 Cancel                                                       |%s%s\n",
                   ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
            draw_box_footer("Select", "Back");
            printf("\nSelect theme [1-3]: ");
            char theme_choice[16];
            read_line(theme_choice, sizeof(theme_choice));
            if (theme_choice[0] == '1') {
                strcpy(cfg->desktop_theme, "dark");
                (void)commit_config(cfg);
            } else if (theme_choice[0] == '2') {
                strcpy(cfg->desktop_theme, "light");
                (void)commit_config(cfg);
            }
        } else if (choice[0] == '4' || choice[0] == 'q' || choice[0] == '\0') {
            break;
        }
    }
}

/* --- Submenu: System Options --- */

static __attribute__((noinline)) void configure_system(struct akaos_config *cfg) {
    for (;;) {
        draw_box_header("System Options", "Configure core system parameters:");
        printf("   %s%s|   1 System Hostname  Set network hostname (Current: %-16s) |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, cfg->hostname, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   2 Audio Bell       Terminal acoustic feedback [%-8s]         |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, cfg->audio_bell ? "ENABLED" : "DISABLED", ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   3 Return to Main Menu                                          |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        draw_box_footer("Select", "Back");

        printf("\nSelect option [1-3] or 'q' to return: ");
        char choice[16];
        read_line(choice, sizeof(choice));

        if (choice[0] == '1') {
            printf("\nCurrent Hostname: %s\n", cfg->hostname);
            printf("Enter new hostname: ");
            char hbuf[64];
            read_line(hbuf, sizeof(hbuf));
            if (hbuf[0] != '\0') {
                strncpy(cfg->hostname, hbuf, sizeof(cfg->hostname) - 1);
                cfg->hostname[sizeof(cfg->hostname) - 1] = '\0';
                (void)commit_config(cfg);
            }
        } else if (choice[0] == '2') {
            cfg->audio_bell = !cfg->audio_bell;
            (void)commit_config(cfg);
        } else if (choice[0] == '3' || choice[0] == 'q' || choice[0] == '\0') {
            break;
        }
    }
}

/* --- Submenu: Interface Options (Daemons) --- */

static __attribute__((noinline)) void configure_interfaces(struct akaos_config *cfg) {
    for (;;) {
        draw_box_header("Interface Options", "Enable or disable network server daemons:");
        printf("   %s%s|   1 Web Server (httpd)        Status: %-8s (Port: %-5d)         |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, cfg->httpd_enabled ? "ENABLED" : "DISABLED", cfg->httpd_port, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   2 Telnet Server (telnetd)   Status: %-8s (Port: %-5d)         |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, cfg->telnetd_enabled ? "ENABLED" : "DISABLED", cfg->telnetd_port, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   3 TFTP Server (tftpd)       Status: %-8s (Port: %-5d)         |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, cfg->tftpd_enabled ? "ENABLED" : "DISABLED", cfg->tftpd_port, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   4 SSH Server (sshd)         Status: %-8s (Port: %-5d)         |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, cfg->sshd_enabled ? "ENABLED" : "DISABLED", cfg->sshd_port, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   5 Return to Main Menu                                          |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        draw_box_footer("Select", "Back");

        printf("\nSelect option [1-5] or 'q' to return: ");
        char choice[16];
        read_line(choice, sizeof(choice));

        if (choice[0] == '1') {
            cfg->httpd_enabled = !cfg->httpd_enabled;
            (void)commit_config(cfg);
        } else if (choice[0] == '2') {
            cfg->telnetd_enabled = !cfg->telnetd_enabled;
            (void)commit_config(cfg);
        } else if (choice[0] == '3') {
            cfg->tftpd_enabled = !cfg->tftpd_enabled;
            (void)commit_config(cfg);
        } else if (choice[0] == '4') {
            cfg->sshd_enabled = !cfg->sshd_enabled;
            (void)commit_config(cfg);
        } else if (choice[0] == '5' || choice[0] == 'q' || choice[0] == '\0') {
            break;
        }
    }
}

/* --- Info Box: Network Information --- */

static void show_network_info(void) {
    draw_box_header("Network Information", "Active hardware interface and routing status:");
    printf("   %s%s|   Interface:    eth0 (VirtIO-Net PCI)                            |%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   IPv4 Address: 10.0.2.15/24 (Broadcast: 10.0.2.255)            |%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   Default GW:   10.0.2.2                                         |%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   DNS Server:   10.0.2.3                                         |%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   MAC Address:  52:54:00:12:34:56                                |%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    draw_box_footer("Ok", NULL);

    printf("\nPress Enter or 'q' to return: ");
    char d[8];
    read_line(d, sizeof(d));
}

/* --- Submenu: Hardware & CPU/GPU Information --- */

static void show_hardware_info(void) {
    draw_box_header("Hardware & System Specifications", "Hardware detected by XSCore kernel:");
    char cpu_model[64] = "Generic x86-64 Processor";
    char gpu_model[64] = "VirtIO-GPU / VESA Framebuffer";
    char mem_info[64] = "256 MB";

    int fd = open("/proc/cpuinfo", O_RDONLY);
    if (fd >= 0) {
        char buf[1024];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *m = strstr(buf, "model name\t: ");
            if (m) {
                m += 13;
                char *nl = strchr(m, '\n');
                if (nl) *nl = '\0';
                strncpy(cpu_model, m, sizeof(cpu_model) - 1);
                cpu_model[sizeof(cpu_model) - 1] = '\0';
            }
        }
    }

    fd = open("/proc/gpuinfo", O_RDONLY);
    if (fd >= 0) {
        char buf[1024];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *m = strstr(buf, "Chipset      : ");
            if (!m) m = strstr(buf, "Driver       : ");
            if (m) {
                char *colon = strchr(m, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ') colon++;
                    char *nl = strchr(colon, '\n');
                    if (nl) *nl = '\0';
                    strncpy(gpu_model, colon, sizeof(gpu_model) - 1);
                    gpu_model[sizeof(gpu_model) - 1] = '\0';
                }
            }
        }
    }

    fd = open("/proc/meminfo", O_RDONLY);
    if (fd >= 0) {
        char buf[512];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *m = strstr(buf, "MemTotal: ");
            if (m) {
                m += 10;
                char *nl = strchr(m, '\n');
                if (nl) *nl = '\0';
                strncpy(mem_info, m, sizeof(mem_info) - 1);
                mem_info[sizeof(mem_info) - 1] = '\0';
            }
        }
    }

    printf("   %s%s|   CPU Model:    %-48s |%s%s\n", ANSI_GRAY_BG, ANSI_BLACK_FG, cpu_model, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   GPU Chipset:  %-48s |%s%s\n", ANSI_GRAY_BG, ANSI_BLACK_FG, gpu_model, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   Memory RAM:   %-48s |%s%s\n", ANSI_GRAY_BG, ANSI_BLACK_FG, mem_info, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   Display HAL:  Framebuffer (/dev/fb0) & X11 Server (:0)         |%s%s\n", ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    draw_box_footer("Ok", NULL);

    printf("\nPress Enter or 'q' to return: ");
    char d[8];
    read_line(d, sizeof(d));
}

/* --- Submenu: Package Manager Status --- */

static void show_package_info(void) {
    draw_box_header("Package Manager (pkg) Status", "Local package manager status and configuration:");
    char mirror_url[96] = "http://mirror.zyqu.pl/akaos";
    int fd = open("/etc/pkg.conf", O_RDONLY);
    if (fd >= 0) {
        char buf[512];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n > 0) {
            buf[n] = '\0';
            char *m = strstr(buf, "mirror=");
            if (m) {
                m += 7;
                char *nl = strchr(m, '\n');
                if (nl) *nl = '\0';
                strncpy(mirror_url, m, sizeof(mirror_url) - 1);
                mirror_url[sizeof(mirror_url) - 1] = '\0';
            }
        }
    }

    printf("   %s%s|   Package Tool: pkg (akaOS native package manager)               |%s%s\n", ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   Archive Type: .akp (POSIX ustar with .PKGINFO metadata)        |%s%s\n", ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   Mirror Server:%-48s |%s%s\n", ANSI_GRAY_BG, ANSI_BLACK_FG, mirror_url, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   Cache Path:   /var/cache/pkg                                   |%s%s\n", ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    draw_box_footer("Ok", NULL);

    printf("\nPress Enter or 'q' to return: ");
    char d[8];
    read_line(d, sizeof(d));
}

/* --- Info Box: About akaOS --- */

static void show_about_info(void) {
    draw_box_header("About akaOS", "Operating system and kernel specifications:");
    printf("   %s%s|   Distribution: akaOS                                            |%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   Kernel:       XSCore Microkernel (Ring 0 SMP)                  |%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   Compositor:   Lumen Shared-Memory Window Compositor            |%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   Desktop:      Aurora Shell (akaOS Native Desktop)              |%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    printf("   %s%s|   Network:      In-Kernel BSD TCP/UDP + VirtIO-Net Hardware      |%s%s\n",
           ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
    draw_box_footer("Ok", NULL);

    printf("\nPress Enter or 'q' to return: ");
    char d[8];
    read_line(d, sizeof(d));
}

/* --- Main raspi-config Full-Screen Dialog Menu --- */

static void interactive_menu(struct akaos_config *cfg) {
    for (;;) {
        draw_box_header("akaOS Software Configuration Tool (akaos-config)",
                        "Select an option below and press Enter:");
        printf("   %s%s|   1 System Options      Configure hostname and audio bell        |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   2 Interface Options   Configure Web, Telnet, SSH, and TFTP     |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   3 Desktop Options     Select Desktop (XFCE/LXDE/Aurora/None)   |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   4 Network Information View IP address, Gateway, DNS, MAC       |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   5 Hardware Specs      Inspect CPU brand, GPU model, and RAM    |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   6 Package Manager     Inspect pkg status and mirror URL        |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   7 About akaOS         Kernel release & license specifications  |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        printf("   %s%s|   8 Finish              Save configuration and exit              |%s%s\n",
               ANSI_GRAY_BG, ANSI_BLACK_FG, ANSI_RESET, ANSI_BLUE_BG);
        draw_box_footer("Select", "Finish");

        printf("\nSelect an option [1-8] or 'q' to exit: ");
        char choice[16];
        read_line(choice, sizeof(choice));

        if (choice[0] == '1') {
            configure_system(cfg);
        } else if (choice[0] == '2') {
            configure_interfaces(cfg);
        } else if (choice[0] == '3') {
            configure_desktop(cfg);
        } else if (choice[0] == '4') {
            show_network_info();
        } else if (choice[0] == '5') {
            show_hardware_info();
        } else if (choice[0] == '6') {
            show_package_info();
        } else if (choice[0] == '7') {
            show_about_info();
        } else if (choice[0] == '8' || choice[0] == 'q' || choice[0] == 'Q' || choice[0] == '\0') {
            printf("%s%s", ANSI_RESET, ANSI_CLEAR_SCREEN);
            puts("akaOS System Configuration saved. Exiting.");
            break;
        }
    }
}

int main(int argc, char **argv) {
    struct akaos_config cfg;
    load_config(&cfg);

    if (argc > 1) {
        int status_requested = 0;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                print_usage();
                return 0;
            } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
                puts("akaos-config (akaOS Configuration System) 2.0");
                return 0;
            } else if (strcmp(argv[i], "--status") == 0) {
                status_requested = 1;
            } else if (strcmp(argv[i], "--hardware") == 0) {
                show_hardware_info();
                return 0;
            } else if (strcmp(argv[i], "--pkg-status") == 0) {
                show_package_info();
                return 0;
            } else if (strcmp(argv[i], "--enable-httpd") == 0) {
                cfg.httpd_enabled = 1;
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: HTTP web server ENABLED on port %d\n", cfg.httpd_port);
            } else if (strcmp(argv[i], "--disable-httpd") == 0) {
                cfg.httpd_enabled = 0;
                if (!commit_config(&cfg)) return 1;
                puts("akaos-config: HTTP web server DISABLED");
            } else if (strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "--http-port") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: %s requires a port number\n", argv[i]);
                    return 2;
                }
                int port = 0;
                if (!parse_port(argv[++i], &port)) {
                    fprintf(2, "akaos-config: error: invalid HTTP port '%s' (use 1-65535)\n", argv[i]);
                    return 2;
                }
                cfg.httpd_port = port;
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: HTTP port set to %d\n", cfg.httpd_port);
            } else if (strcmp(argv[i], "--enable-telnetd") == 0 || strcmp(argv[i], "--enable-telnet") == 0) {
                cfg.telnetd_enabled = 1;
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: Telnet daemon ENABLED on port %d\n", cfg.telnetd_port);
            } else if (strcmp(argv[i], "--disable-telnetd") == 0 || strcmp(argv[i], "--disable-telnet") == 0) {
                cfg.telnetd_enabled = 0;
                if (!commit_config(&cfg)) return 1;
                puts("akaos-config: Telnet daemon DISABLED");
            } else if (strcmp(argv[i], "--set-telnet") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: --set-telnet requires 0 or 1\n");
                    return 2;
                }
                if (!parse_toggle(argv[++i], &cfg.telnetd_enabled)) {
                    fprintf(2, "akaos-config: error: invalid Telnet toggle '%s' (use 0 or 1)\n", argv[i]);
                    return 2;
                }
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: Telnet daemon %s on port %d\n",
                       cfg.telnetd_enabled ? "ENABLED" : "DISABLED", cfg.telnetd_port);
            } else if (strcmp(argv[i], "--telnet-port") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: --telnet-port requires a port number\n");
                    return 2;
                }
                int port = 0;
                if (!parse_port(argv[++i], &port)) {
                    fprintf(2, "akaos-config: error: invalid Telnet port '%s' (use 1-65535)\n", argv[i]);
                    return 2;
                }
                cfg.telnetd_port = port;
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: Telnet port set to %d\n", cfg.telnetd_port);
            } else if (strcmp(argv[i], "--enable-tftpd") == 0 || strcmp(argv[i], "--enable-tftp") == 0) {
                cfg.tftpd_enabled = 1;
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: TFTP daemon ENABLED on port %d\n", cfg.tftpd_port);
            } else if (strcmp(argv[i], "--disable-tftpd") == 0 || strcmp(argv[i], "--disable-tftp") == 0) {
                cfg.tftpd_enabled = 0;
                if (!commit_config(&cfg)) return 1;
                puts("akaos-config: TFTP daemon DISABLED");
            } else if (strcmp(argv[i], "--set-tftp") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: --set-tftp requires 0 or 1\n");
                    return 2;
                }
                if (!parse_toggle(argv[++i], &cfg.tftpd_enabled)) {
                    fprintf(2, "akaos-config: error: invalid TFTP toggle '%s' (use 0 or 1)\n", argv[i]);
                    return 2;
                }
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: TFTP daemon %s on port %d\n",
                       cfg.tftpd_enabled ? "ENABLED" : "DISABLED", cfg.tftpd_port);
            } else if (strcmp(argv[i], "--tftp-port") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: --tftp-port requires a port number\n");
                    return 2;
                }
                int port = 0;
                if (!parse_port(argv[++i], &port)) {
                    fprintf(2, "akaos-config: error: invalid TFTP port '%s' (use 1-65535)\n", argv[i]);
                    return 2;
                }
                cfg.tftpd_port = port;
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: TFTP port set to %d\n", cfg.tftpd_port);
            } else if (strcmp(argv[i], "--enable-sshd") == 0 || strcmp(argv[i], "--enable-ssh") == 0) {
                cfg.sshd_enabled = 1;
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: SSH daemon ENABLED on port %d\n", cfg.sshd_port);
            } else if (strcmp(argv[i], "--disable-sshd") == 0 || strcmp(argv[i], "--disable-ssh") == 0) {
                cfg.sshd_enabled = 0;
                if (!commit_config(&cfg)) return 1;
                puts("akaos-config: SSH daemon DISABLED");
            } else if (strcmp(argv[i], "--set-sshd") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: --set-sshd requires 0 or 1\n");
                    return 2;
                }
                if (!parse_toggle(argv[++i], &cfg.sshd_enabled)) {
                    fprintf(2, "akaos-config: error: invalid SSH toggle '%s' (use 0 or 1)\n", argv[i]);
                    return 2;
                }
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: SSH daemon %s on port %d\n",
                       cfg.sshd_enabled ? "ENABLED" : "DISABLED", cfg.sshd_port);
            } else if (strcmp(argv[i], "--sshd-port") == 0 || strcmp(argv[i], "--ssh-port") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: %s requires a port number\n", argv[i]);
                    return 2;
                }
                int port = 0;
                if (!parse_port(argv[++i], &port)) {
                    fprintf(2, "akaos-config: error: invalid SSH port '%s' (use 1-65535)\n", argv[i]);
                    return 2;
                }
                cfg.sshd_port = port;
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: SSH port set to %d\n", cfg.sshd_port);
            } else if (strcmp(argv[i], "--autostart-desktop") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: --autostart-desktop requires 0 or 1\n");
                    return 2;
                }
                if (!parse_toggle(argv[++i], &cfg.autostart_desktop)) {
                    fprintf(2, "akaos-config: error: invalid desktop autostart '%s' (use 0 or 1)\n", argv[i]);
                    return 2;
                }
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: Desktop autostart %s\n",
                       cfg.autostart_desktop ? "ENABLED" : "DISABLED");
            } else if (strcmp(argv[i], "--enable-autostart-desktop") == 0) {
                cfg.autostart_desktop = 1;
                if (!commit_config(&cfg)) return 1;
                puts("akaos-config: Desktop autostart ENABLED");
            } else if (strcmp(argv[i], "--disable-autostart-desktop") == 0) {
                cfg.autostart_desktop = 0;
                if (!commit_config(&cfg)) return 1;
                puts("akaos-config: Desktop autostart DISABLED");
            } else if (strcmp(argv[i], "--desktop-env") == 0 || strcmp(argv[i], "--env") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: %s requires an environment name\n", argv[i]);
                    return 2;
                }
                const char *env = argv[++i];
                const char *bin = resolve_desktop_path(env);
                if (!bin || !is_desktop_installed(bin)) {
                    printf("akaos-config: error: desktop environment '%s' is not installed\n", env);
                    return 1;
                }
                strncpy(cfg.desktop_env, env, sizeof(cfg.desktop_env) - 1);
                cfg.desktop_env[sizeof(cfg.desktop_env) - 1] = '\0';
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: Desktop environment set to %s\n", cfg.desktop_env);
            } else if (strcmp(argv[i], "--hostname") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: --hostname requires a name\n");
                    return 2;
                }
                const char *hostname = argv[++i];
                if (!valid_hostname(hostname)) {
                    fprintf(2, "akaos-config: error: invalid hostname '%s'\n", hostname);
                    return 2;
                }
                strncpy(cfg.hostname, hostname, sizeof(cfg.hostname) - 1);
                cfg.hostname[sizeof(cfg.hostname) - 1] = '\0';
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: Hostname set to %s\n", cfg.hostname);
            } else if (strcmp(argv[i], "--theme") == 0) {
                if (i + 1 >= argc) {
                    fprintf(2, "akaos-config: error: --theme requires dark or light\n");
                    return 2;
                }
                const char *theme = argv[++i];
                if (strcmp(theme, "dark") != 0 && strcmp(theme, "light") != 0) {
                    fprintf(2, "akaos-config: error: invalid theme '%s' (use dark or light)\n",
                            theme);
                    return 2;
                }
                strcpy(cfg.desktop_theme, theme);
                if (!commit_config(&cfg)) return 1;
                printf("akaos-config: Desktop theme set to %s (applies next session)\n",
                       cfg.desktop_theme);
            } else {
                printf("akaos-config: unrecognized option '%s'\n", argv[i]);
                return 2;
            }
        }
        if (status_requested) print_status(&cfg);
        return 0;
    }

    interactive_menu(&cfg);
    return 0;
}
