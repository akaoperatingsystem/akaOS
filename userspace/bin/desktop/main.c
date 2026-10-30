#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <akaos/display.h>
#include <akaos/dbus.h>
#include <sys/socket.h>
#include <sys/un.h>

static void print_usage(void) {
    puts("Usage: desktop [options]\n"
         "akaOS Desktop Environment Launcher\n\n"
         "Options:\n"
         "  --env <name>  Specify installed desktop environment (default: aurora)\n"
         "                aurora, lxde, lxqt, gnome, kde/plasma\n"
         "  --check       Report runtime readiness without launching a desktop\n"
         "  --apps        Launch default GUI applications (notepad, taskmgr)\n"
         "  --once        Run one composed session cycle and return\n"
         "  -h, --help    Display this help and exit");
}

static const char *resolve_desktop_binary(const char *env) {
    if (!env || env[0] == '\0' || strcmp(env, "aurora") == 0) return "/bin/aurora";
    if (strcmp(env, "lxde") == 0) return "/bin/startlxde";
    if (strcmp(env, "lxqt") == 0) return "/bin/startlxqt";
    if (strcmp(env, "kde") == 0 || strcmp(env, "plasma") == 0) return "/bin/plasma";
    if (strcmp(env, "gnome") == 0 || strcmp(env, "shell") == 0) return "/bin/gnome-shell";
    if (strcmp(env, "xfce") == 0 || strcmp(env, "classic") == 0) return "/bin/xfce4-session";
    return NULL;
}

static int is_binary_installed(const char *path) {
    if (!path) return 0;
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return 1;
    }
    return 0;
}

/* Keep readiness metadata in code rather than pointer tables.  akaOS user
 * images are currently static PIEs without a dynamic relocator; string
 * literals referenced by code remain RIP-relative on every target, while a
 * data table of absolute char pointers would fault when loaded at the native
 * user base. */
static const char *profile_label(const char *id) {
    if (strcmp(id, "aurora") == 0) return "Aurora";
    if (strcmp(id, "lxde") == 0) return "LXDE";
    if (strcmp(id, "lxqt") == 0) return "LXQt";
    if (strcmp(id, "gnome") == 0) return "GNOME";
    if (strcmp(id, "kde") == 0) return "KDE Plasma";
    return "Unknown";
}

static const char *profile_id(const char *id) {
    if (!id) return "";
    if (strcmp(id, "plasma") == 0) return "kde";
    if (strcmp(id, "shell") == 0) return "gnome";
    return id;
}

static size_t profile_requirement_count(const char *id) {
    if (strcmp(id, "aurora") == 0) return 2;
    if (strcmp(id, "lxde") == 0) return 10;
    if (strcmp(id, "lxqt") == 0) return 11;
    if (strcmp(id, "gnome") == 0) return 11;
    if (strcmp(id, "kde") == 0) return 11;
    return 0;
}

static const char *profile_requirement(const char *id, size_t index) {
    if (strcmp(id, "aurora") == 0) {
        if (index == 0) return "/bin/lumen";
        if (index == 1) return "/bin/aurora";
    } else if (strcmp(id, "lxde") == 0) {
        if (index == 0) return "/bin/startlxde";
        if (index == 1) return "/bin/openbox";
        if (index == 2) return "/bin/pcmanfm";
        if (index == 3) return "/bin/dbus-daemon";
        if (index == 4) return "/lib/libpthread.so";
        if (index == 5) return "/lib/libgtk.so";
        if (index == 6) return "/lib/libglib.so";
        if (index == 7) return "/bin/xsdisplay";
        if (index == 8) return "/bin/x11-server";
        if (index == 9) return "/bin/wayland-compositor";
    } else if (strcmp(id, "lxqt") == 0) {
        if (index == 0) return "/bin/startlxqt";
        if (index == 1) return "/bin/lxqt-panel";
        if (index == 2) return "/bin/pcmanfm-qt";
        if (index == 3) return "/bin/dbus-daemon";
        if (index == 4) return "/lib/libpthread.so";
        if (index == 5) return "/lib/libQt6Core.so";
        if (index == 6) return "/lib/libQt6Gui.so";
        if (index == 7) return "/lib/libQt6Widgets.so";
        if (index == 8) return "/bin/xsdisplay";
        if (index == 9) return "/bin/x11-server";
        if (index == 10) return "/bin/wayland-compositor";
    } else if (strcmp(id, "gnome") == 0) {
        if (index == 0) return "/bin/gnome-shell";
        if (index == 1) return "/bin/mutter";
        if (index == 2) return "/bin/dbus-daemon";
        if (index == 3) return "/lib/libpthread.so";
        if (index == 4) return "/lib/libgtk-4.so";
        if (index == 5) return "/lib/libglib.so";
        if (index == 6) return "/lib/libgio.so";
        if (index == 7) return "/bin/xsdisplay";
        if (index == 8) return "/bin/gnome-session";
        if (index == 9) return "/bin/x11-server";
        if (index == 10) return "/bin/wayland-compositor";
    } else if (strcmp(id, "kde") == 0) {
        if (index == 0) return "/bin/plasma";
        if (index == 1) return "/bin/kwin";
        if (index == 2) return "/bin/dbus-daemon";
        if (index == 3) return "/lib/libpthread.so";
        if (index == 4) return "/lib/libQt6Core.so";
        if (index == 5) return "/lib/libQt6Gui.so";
        if (index == 6) return "/lib/libQt6Widgets.so";
        if (index == 7) return "/bin/xsdisplay";
        if (index == 8) return "/bin/startplasma";
        if (index == 9) return "/bin/x11-server";
        if (index == 10) return "/bin/wayland-compositor";
    }
    return NULL;
}

static void print_profile_status(const char *id) {
    id = profile_id(id);
    size_t required_count = profile_requirement_count(id);
    size_t missing = 0;
    printf("%s (%s): ", profile_label(id), id);
    for (size_t i = 0; i < required_count; ++i) {
        if (!is_binary_installed(profile_requirement(id, i))) ++missing;
    }
    if (missing == 0) {
        if (strcmp(id, "aurora") != 0 &&
            is_binary_installed("/etc/akaos-desktop-compat")) {
            puts("COMPATIBILITY ONLY (upstream session not installed)");
        } else {
            puts("READY");
        }
        return;
    }
    printf("NOT READY (missing %d requirement%s)\n", (int)missing,
           missing == 1 ? "" : "s");
    for (size_t i = 0; i < required_count; ++i) {
        const char *path = profile_requirement(id, i);
        if (!is_binary_installed(path)) printf("  - %s\n", path);
    }
}

static void print_readiness(const char *requested) {
    puts("akaOS desktop runtime readiness");
    puts("Stock KDE/GNOME/LXDE/LXQt sessions are not installed; their profile commands are compatibility frontends.");
    if (is_binary_installed("/usr/local/plugins/platforms/libqwayland.so") &&
        is_binary_installed("/usr/local/lib/.upstream-qtbase"))
        puts("Qt 6 Wayland QPA runtime: INSTALLED");
    if (requested) {
        requested = profile_id(requested);
        if (profile_requirement_count(requested) == 0) {
            printf("Unknown desktop environment '%s'\n", requested);
            return;
        }
        print_profile_status(requested);
        return;
    }
    print_profile_status("aurora");
    print_profile_status("lxde");
    print_profile_status("lxqt");
    print_profile_status("gnome");
    print_profile_status("kde");
}

static int is_compositor_running(void) {
    int fd = shm_open(AKAOS_COMPOSITOR_IPC_PATH, O_RDWR, 0666);
    if (fd >= 0) {
        close(fd);
        return 1;
    }
    return 0;
}

static int is_wayland_socket_ready(void) {
    static const char socket_path[] = "/run/user/0/wayland-0";
    int descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) return 0;

    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, sizeof(socket_path));
    int ready = connect(descriptor, (const struct sockaddr *)&address,
                        sizeof(address)) == 0;
    close(descriptor);
    return ready;
}

static void ensure_wayland_bridge(void) {
    if (is_wayland_socket_ready() ||
        !is_binary_installed("/bin/wayland-bridge"))
        return;

    char bridge_path[] = "/bin/wayland-bridge";
    char *bridge_args[] = {bridge_path, NULL};
    if (spawn(bridge_path, bridge_args) < 0) {
        fprintf(2, "[desktop] Warning: unable to start Wayland bridge (errno=%d)\n",
                errno);
        return;
    }

    for (int attempt = 0; attempt < 200; ++attempt) {
        poll(NULL, 0, 50);
        if (is_wayland_socket_ready()) {
            puts("[desktop] Wayland client socket is ready.");
            return;
        }
    }
    puts("[desktop] Warning: Wayland bridge did not publish wayland-0.");
}

static int ensure_session_bus(void) {
    int fd = akaos_dbus_connect(0);
    if (fd >= 0) {
        (void)akaos_dbus_close(fd);
        return 1;
    }

    /* Desktop sessions own the per-user bus.  Start the native daemon only
     * for a real graphical launch; --check stays side-effect free. */
    char daemon_path[] = "/bin/dbus-daemon";
    char daemon_mode[] = "--session";
    char *daemon_args[] = {daemon_path, daemon_mode, NULL};
    pid_t daemon_pid = spawn(daemon_path, daemon_args);
    if (daemon_pid < 0) {
        fprintf(2, "[desktop] Warning: unable to start session bus (errno=%d)\n",
                errno);
        return 0;
    }

    int last_error = errno;
    for (int attempt = 0; attempt < 100; ++attempt) {
        fd = akaos_dbus_connect(0);
        if (fd >= 0) {
            (void)akaos_dbus_close(fd);
            puts("[desktop] Session D-Bus bus started.");
            return 1;
        }
        last_error = errno;
        (void)poll(NULL, 0, 50);
    }
    fprintf(2, "[desktop] Warning: session bus did not become ready (errno=%d)\n",
            last_error);
    return 0;
}

static void get_configured_desktop_env(char *dest, size_t max_len) {
    strncpy(dest, "aurora", max_len - 1);
    dest[max_len - 1] = '\0';

    int fd = open("/etc/akaos.conf", O_RDONLY);
    if (fd < 0) return;

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    char *p = strstr(buf, "DESKTOP_ENV=");
    if (p) {
        p += 12;
        size_t idx = 0;
        while (*p && *p != '\n' && *p != '\r' && idx < max_len - 1) {
            dest[idx++] = *p++;
        }
        dest[idx] = '\0';
    }
}

int main(int argc, char **argv) {
    int launch_apps = 0;
    int check_only = 0;
    int once = 0;
    int env_was_set = 0;
    char desktop_env[32];
    get_configured_desktop_env(desktop_env, sizeof(desktop_env));

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(argv[i], "--check") == 0) {
            check_only = 1;
        } else if (strcmp(argv[i], "--apps") == 0) {
            launch_apps = 1;
        } else if (strcmp(argv[i], "--once") == 0) {
            once = 1;
        } else if ((strcmp(argv[i], "--env") == 0 || strcmp(argv[i], "-e") == 0) && i + 1 < argc) {
            strncpy(desktop_env, argv[++i], sizeof(desktop_env) - 1);
            desktop_env[sizeof(desktop_env) - 1] = '\0';
            env_was_set = 1;
        }
    }

    if (check_only) {
        print_readiness(env_was_set ? desktop_env : NULL);
        return 0;
    }

    const char *path_environment = getenv("PATH");
    if (!path_environment || !path_environment[0])
        (void)setenv("PATH", "/bin:/usr/bin:/usr/local/bin", 1);

    (void)setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/user/0/bus", 1);
    (void)setenv("DBUS_SYSTEM_BUS_ADDRESS",
                 "unix:path=/run/dbus/system_bus_socket", 1);
    if (access("/etc/akaos-dbus-client-trace", F_OK) == 0)
        (void)setenv("DBUS_VERBOSE", "1", 1);
    (void)setenv("XDG_RUNTIME_DIR", "/run/user/0", 1);
    (void)setenv("WAYLAND_DISPLAY", "wayland-0", 1);

    if (!ensure_session_bus()) return 1;

    if (!is_compositor_running()) {
        printf("[desktop] Starting Lumen window compositor...\n");
        char lumen_path[] = "/bin/lumen";
        char *lumen_args[] = {lumen_path, NULL};
        pid_t lpid = spawn(lumen_path, lumen_args);
        if (lpid < 0) {
            fprintf(2, "[desktop] Error: unable to start Lumen compositor.\n");
            return 1;
        }

        /* Lumen needs time to initialize its framebuffer and input devices on
         * a cold boot.  A one-second limit raced the autostart login path and
         * left the shell visible without a desktop. */
        int ready = 0;
        for (int i = 0; i < 200; ++i) {
            poll(NULL, 0, 50);
            if (is_compositor_running()) {
                ready = 1;
                break;
            }
        }
        if (!ready) {
            fprintf(2, "[desktop] Error: timed out waiting for Lumen compositor.\n");
            return 1;
        }
    }

    ensure_wayland_bridge();

    const char *bin = resolve_desktop_binary(desktop_env);
    if (!bin || !is_binary_installed(bin)) {
        fprintf(2, "[desktop] Error: desktop environment '%s' is not installed.\n", desktop_env);
        return 1;
    }

    printf("[desktop] Starting %s desktop environment...\n", desktop_env);
    char desktop_path[64];
    strncpy(desktop_path, bin, sizeof(desktop_path) - 1);
    desktop_path[sizeof(desktop_path) - 1] = '\0';
    /* Start a composed profile session: the profile shell owns the primary
     * surface and starts its window-manager/panel/session companions. */
    char session_mode[] = "--session";
    char once_mode[] = "--once";
    char *desktop_args[] = {desktop_path, session_mode,
                            once ? once_mode : NULL, NULL};
    pid_t apid = spawn(desktop_path, desktop_args);
    if (apid < 0) {
        fprintf(2, "[desktop] Error: unable to start %s desktop environment.\n", desktop_env);
        return 1;
    }

    if (launch_apps) {
        poll(NULL, 0, 100);
        char notepad_path[] = "/bin/notepad";
        char *notepad_args[] = {notepad_path, NULL};
        pid_t npid = spawn(notepad_path, notepad_args);
        if (npid < 0) {
            fprintf(2, "[desktop] Warning: unable to launch default notepad app.\n");
        }
    }

    printf("[desktop] akaOS Desktop Environment (%s) is running.\n", desktop_env);
    /* Keep the launcher attached to the session's lifetime, like a Linux
     * display-manager session.  A clean profile exit returns control to the
     * caller instead of leaving an untracked desktop process behind. */
    int status = 0;
    (void)waitpid(apid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
