#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int cfd = open("/dev/tty1", O_WRONLY);
    if (cfd >= 0) {
        const char msg[] = "[startlxde] Launching official LXDE components (openbox, pcmanfm, lxpanel)...\n";
        (void)write(cfd, msg, sizeof(msg) - 1);
        close(cfd);
    }

    /* 1. Launch Openbox Window Manager */
    pid_t ob_pid = fork();
    if (ob_pid == 0) {
        char *ob_argv[] = {"openbox", NULL};
        execv("/usr/local/bin/openbox", ob_argv);
        execv("/bin/openbox", ob_argv);
        _exit(127);
    }

    usleep(300000);

    /* 2. Launch PCManFM Desktop Manager (draws wallpaper and desktop shortcuts) */
    pid_t fm_pid = fork();
    if (fm_pid == 0) {
        char *fm_argv[] = {"pcmanfm", "--desktop", "--profile", "LXDE", NULL};
        execv("/usr/local/bin/pcmanfm", fm_argv);
        execv("/bin/pcmanfm", fm_argv);
        _exit(127);
    }

    usleep(300000);

    /* 3. Launch LXPanel (draws taskbar, start menu, system tray, and clock) */
    char *panel_argv[] = {"lxpanel", "--profile", "LXDE", NULL};
    execv("/usr/local/bin/lxpanel", panel_argv);
    execv("/bin/lxpanel", panel_argv);
    return 127;
}
