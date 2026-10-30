#include <akaos/account.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        if (setgid(account.gid) != 0 || setuid(account.uid) != 0) {
            puts("login: unable to establish credentials");
            return 1;
        }
        /* Standard desktop clients inherit these conventional D-Bus and
         * XDG locations from the authenticated login session. */
        (void)setenv("DBUS_SYSTEM_BUS_ADDRESS",
                     "unix:path=/run/dbus/system_bus_socket", 1);
        (void)setenv("DBUS_SESSION_BUS_ADDRESS",
                     "unix:path=/run/user/0/bus", 1);
        (void)setenv("XDG_RUNTIME_DIR", "/run/user/0", 1);
        (void)setenv("WAYLAND_DISPLAY", "wayland-0", 1);
        if (chdir(account.home) != 0) chdir("/");
        if (check_autostart_desktop()) {
            puts("[desktop] Autostart enabled; launching desktop session...");
            /* Use the kernel's spawn path directly.  A fork followed by an
             * execve from the setuid login image can retain a stale user
             * argument mapping during the very early boot handoff; spawn
             * creates the child and copies argv atomically. */
            char desktop_path[] = "/bin/desktop";
            char desktop_option[] = "--apps";
            char *dargs[] = {desktop_path, desktop_option, NULL};
            pid_t dpid = spawn(desktop_path, dargs);
            if (dpid < 0) {
                fprintf(2, "[desktop] Unable to launch desktop session (errno=%d)\n", errno);
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
