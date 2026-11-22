#include <akaos/account.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int check_autostart_desktop(void) {
    int fd = open("/etc/akaos.conf", O_RDONLY);
    if (fd < 0) return 0;
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    /* Match a complete configuration line.  A substring search can accept a
     * malformed value (or a value embedded in a comment) and made the login
     * path disagree with akaos-config after a persistent reboot. */
    const char key[] = "AUTOSTART_DESKTOP=1";
    const size_t key_length = sizeof(key) - 1;
    char *line = buf;
    while (*line) {
        char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        if (length > 0 && line[length - 1] == '\r') --length;
        if (length == key_length && memcmp(line, key, key_length) == 0) return 1;
        if (!end) break;
        line = end + 1;
    }
    return 0;
}

static int read_line(const char *prompt, char *buffer, size_t capacity) {

    if (prompt) write(STDOUT_FILENO, prompt, strlen(prompt));
    size_t used = 0;
    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) return 0;
        if (c == '\n' || c == '\r') break;
        if (used + 1 < capacity) buffer[used++] = c;
    }
    buffer[used] = 0;
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: login\nAuthenticate and start an akaOS session.");
        return 0;
    }
    if (argc != 1) {
        puts("login: invalid option or extra operand");
        return 1;
    }
    int fail_count = 0;
    for (;;) {
        char name[AKAOS_ACCOUNT_NAME_MAX + 1];
        char password[AKAOS_PASSWORD_MAX + 1];
        struct akaos_account account;
        if (check_autostart_desktop()) {
            if (akaos_account_by_name("root", &account)) {
                goto perform_authenticated_session;
            }
        }
        puts("akaOS tty1");
        if (!read_line("login: ", name, sizeof(name))) continue;
        int known = akaos_account_by_name(name, &account);
        if (!akaos_read_secret("Password: ", password, sizeof(password))) continue;
        if (!known || !akaos_account_verify(&account, password)) {
            memset(password, 0, sizeof(password));
            ++fail_count;
            if (fail_count > 1) {
                poll(NULL, 0, (fail_count > 5 ? 5 : fail_count) * 200);
            }
            puts("Login incorrect\n");
            continue;
        }
        fail_count = 0;
        memset(password, 0, sizeof(password));
perform_authenticated_session:
        if (setgid(account.gid) != 0 || setuid(account.uid) != 0) {
            puts("login: unable to establish credentials");
            return 1;
        }
        /* Standard desktop clients inherit these conventional D-Bus, X11, and
         * XDG locations from the authenticated login session. */
        (void)setenv("DBUS_SYSTEM_BUS_ADDRESS",
                     "unix:path=/run/dbus/system_bus_socket", 1);
        (void)setenv("DBUS_SESSION_BUS_ADDRESS",
                     "unix:path=/run/user/0/bus", 1);
        (void)setenv("XDG_RUNTIME_DIR", "/run/user/0", 1);
        (void)setenv("WAYLAND_DISPLAY", "wayland-0", 1);
        (void)setenv("DISPLAY", ":0", 1);
        (void)setenv("XDG_DATA_DIRS", "/usr/local/share:/usr/share", 1);
        (void)setenv("XDG_CONFIG_DIRS", "/etc/xdg:/usr/local/etc/xdg", 1);
        (void)setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
        (void)setenv("HOME", account.home[0] ? account.home : "/root", 1);
        (void)setenv("USER", account.name[0] ? account.name : "root", 1);
        (void)setenv("LOGNAME", account.name[0] ? account.name : "root", 1);
        (void)setenv("SHELL", account.shell[0] ? account.shell : "/bin/akash", 1);
        if (chdir(account.home) != 0) chdir("/");
        if (check_autostart_desktop()) {
            char env_buf[64] = "lxde";
            int fd = open("/etc/akaos.conf", O_RDONLY);
            if (fd >= 0) {
                char conf_buf[512];
                ssize_t n = read(fd, conf_buf, sizeof(conf_buf) - 1);
                close(fd);
                if (n > 0) {
                    conf_buf[n] = '\0';
                    char *p = strstr(conf_buf, "DESKTOP_ENV=");
                    if (p) {
                        p += 12;
                        size_t idx = 0;
                        while (*p && *p != '\n' && *p != '\r' && idx < sizeof(env_buf) - 1) {
                            env_buf[idx++] = *p++;
                        }
                        env_buf[idx] = '\0';
                    }
                }
            }
            if (strcmp(env_buf, "lxqt") == 0) {
                (void)setenv("GDK_BACKEND", "wayland", 1);
                (void)setenv("QT_QPA_PLATFORM", "wayland", 1);
            } else {
                (void)setenv("GDK_BACKEND", "x11,wayland", 1);
            }
            (void)mkdir("/run", 0755);
            (void)mkdir("/run/user", 0755);
            (void)mkdir("/run/user/0", 0700);
            /* Start display compositor (lumen) */
            if (access("/bin/lumen", X_OK) == 0) {
                char lm_path[] = "/bin/lumen";
                char *lm_args[] = {lm_path, NULL};
                (void)spawn(lm_path, lm_args);
                (void)usleep(300000);
            }
            /* Start Wayland server bridge */
            if (access("/bin/wayland-bridge", X_OK) == 0) {
                char wb_path[] = "/bin/wayland-bridge";
                char *wb_args[] = {wb_path, NULL};
                (void)spawn(wb_path, wb_args);
                (void)usleep(300000);
            }
            /* Start native X11 server */
            if (access("/bin/xserver", X_OK) == 0) {
                char xs_path[] = "/bin/xserver";
                char *xs_args[] = {xs_path, NULL};
                (void)spawn(xs_path, xs_args);
                (void)usleep(300000);
            }
            /* Fallback protocol bridge if full servers absent */
            if (access("/bin/wayland-bridge", X_OK) != 0 && access("/bin/xserver", X_OK) != 0 &&
                access("/bin/display-protocols", X_OK) == 0) {
                char dp_path[] = "/bin/display-protocols";
                char dp_opt[] = "--both";
                char *dp_args[] = {dp_path, dp_opt, NULL};
                (void)spawn(dp_path, dp_args);
                (void)usleep(250000);
            }
            const char *session_path = NULL;
            char *dargs[8] = {NULL};
            if (strcmp(env_buf, "lxde") == 0) {
                session_path = access("/usr/local/bin/startlxde", X_OK) == 0 ? "/usr/local/bin/startlxde" : "/bin/startlxde";
                dargs[0] = (char *)session_path;
                dargs[1] = NULL;
            } else if (strcmp(env_buf, "lxqt") == 0) {
                session_path = access("/bin/startlxqt", X_OK) == 0 ? "/bin/startlxqt" : "/usr/local/bin/startlxqt";
                dargs[0] = (char *)session_path;
                dargs[1] = NULL;
            } else if (strcmp(env_buf, "openbox") == 0) {
                if (access("/bin/x11-client", X_OK) == 0) {
                    char cl_path[] = "/bin/x11-client";
                    char *cl_args[] = {cl_path, NULL};
                    (void)spawn(cl_path, cl_args);
                }
                session_path = access("/usr/local/bin/openbox", X_OK) == 0 ? "/usr/local/bin/openbox" : "/bin/openbox";
                dargs[0] = (char *)session_path;
                dargs[1] = NULL;
            } else if (strcmp(env_buf, "xfce") == 0) {
                if (access("/bin/x11-client", X_OK) == 0) {
                    char cl_path[] = "/bin/x11-client";
                    char *cl_args[] = {cl_path, NULL};
                    (void)spawn(cl_path, cl_args);
                }
                session_path = access("/usr/local/bin/xfce4-session", X_OK) == 0 ? "/usr/local/bin/xfce4-session" : "/bin/xfce4-session";
                dargs[0] = (char *)session_path;
                dargs[1] = NULL;
            }
            if (!session_path || access(session_path, X_OK) != 0) {
                fprintf(2, "[session] Official %s session is not installed; refusing an imitation.\n", env_buf);
            } else {
                printf("[session] Autostart enabled; launching official %s (%s)...\n", env_buf, session_path);
                pid_t dpid = spawn(session_path, dargs);
                if (dpid < 0)
                    fprintf(2, "[session] Unable to launch %s (errno=%d)\n", session_path, errno);
            }
        }
        char *shell_argv[] = {account.shell, NULL};

        int pid = spawn(account.shell, shell_argv);
        if (pid < 0) {
            puts("login: unable to start shell");
            return 1;
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return status;
    }
}
