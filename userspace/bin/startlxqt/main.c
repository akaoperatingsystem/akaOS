#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int set_default(const char *name, const char *value) {
    const char *current = getenv(name);
    if (current && current[0]) return 0;
    return setenv(name, value, 1);
}

static int set_home_default(const char *name, const char *suffix) {
    if (getenv(name)) return 0;
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/root";
    char path[512];
    int length = snprintf(path, sizeof(path), "%s/%s", home, suffix);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return setenv(name, path, 1);
}

int main(void) {
    if (set_default("PATH", "/bin:/usr/bin:/usr/local/bin") != 0 ||
        set_default("HOME", "/root") != 0 ||
        set_home_default("XDG_DATA_HOME", ".local/share") != 0 ||
        set_home_default("XDG_CONFIG_HOME", ".config") != 0 ||
        set_home_default("XDG_CACHE_HOME", ".cache") != 0 ||
        set_default("XDG_CONFIG_DIRS", "/etc:/etc/xdg:/usr/share") != 0 ||
        set_default("XDG_SESSION_TYPE", "wayland") != 0 ||
        set_default("WAYLAND_DISPLAY", "wayland-0") != 0 ||
        set_default("XDG_RUNTIME_DIR", "/run/user/0") != 0 ||
        set_default("QT_QPA_PLATFORM", "wayland") != 0 ||
        set_default("QT_QPA_PLATFORMTHEME", "lxqt") != 0 ||
        set_default("XDG_MENU_PREFIX", "lxqt-") != 0 ||
        set_default("XDG_CURRENT_DESKTOP", "LXQt") != 0) {
        fprintf(stderr, "startlxqt: failed to prepare the session environment (errno=%d)\n",
                errno);
        return 1;
    }

    const char *existing_data_dirs = getenv("XDG_DATA_DIRS");
    if (!existing_data_dirs || !existing_data_dirs[0]) {
        char data_dirs[1024];
        const char *data_home = getenv("XDG_DATA_HOME");
        int length = snprintf(data_dirs, sizeof(data_dirs),
                              "%s:/usr/local/share:/usr/share", data_home);
        if (length < 0 || (size_t)length >= sizeof(data_dirs) ||
            setenv("XDG_DATA_DIRS", data_dirs, 1) != 0) {
            fprintf(stderr, "startlxqt: failed to prepare XDG_DATA_DIRS (errno=%d)\n",
                    errno);
            return 1;
        }
    } else if (!strstr(existing_data_dirs, "/usr/share")) {
        char data_dirs[1024];
        int length = snprintf(data_dirs, sizeof(data_dirs),
                              "%s:/usr/share", existing_data_dirs);
        if (length < 0 || (size_t)length >= sizeof(data_dirs) ||
            setenv("XDG_DATA_DIRS", data_dirs, 1) != 0) {
            fprintf(stderr, "startlxqt: failed to extend XDG_DATA_DIRS (errno=%d)\n",
                    errno);
            return 1;
        }
    }

    const char *home = getenv("HOME");
    if (!home || !home[0]) home = "/root";
    char desktop_dir[512];
    int length = snprintf(desktop_dir, sizeof(desktop_dir), "%s/Desktop", home);
    if (length < 0 || (size_t)length >= sizeof(desktop_dir)) {
        fprintf(stderr, "startlxqt: desktop directory path is too long\n");
        return 1;
    }
    if (mkdir(desktop_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "startlxqt: cannot create %s (errno=%d)\n",
                desktop_dir, errno);
        return 1;
    }

    char session_path[] = "/bin/lxqt-session";
    char *session_args[] = {session_path, NULL};
    execv(session_path, session_args);
    fprintf(stderr, "startlxqt: cannot execute %s (errno=%d)\n",
            session_path, errno);
    return 127;
}
