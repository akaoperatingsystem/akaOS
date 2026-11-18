/*
 * akaOS Official LXDE Session Launcher
 *
 * Prepares the standard XDG/X11 environment and launches the official,
 * unmodified upstream LXDE desktop components:
 * - Window Manager: /usr/local/bin/openbox or /bin/openbox
 * - Desktop & Files: /usr/local/bin/pcmanfm --desktop
 * - Desktop Panel:   /usr/local/bin/lxpanel
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int is_installed(const char *path) {
    if (!path) return 0;
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        return 1;
    }
    return 0;
}

static int set_default(const char *name, const char *value) {
    const char *current = getenv(name);
    if (current && current[0]) return 0;
    return setenv(name, value, 1);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    /* Set up standard X11 and XDG environment for LXDE */
    set_default("PATH", "/bin:/usr/bin:/usr/local/bin");
    set_default("HOME", "/root");
    set_default("DISPLAY", ":0");
    set_default("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
    set_default("XDG_CONFIG_DIRS", "/etc/xdg:/usr/local/etc/xdg");
    set_default("XDG_MENU_PREFIX", "lxde-");
    set_default("XDG_CURRENT_DESKTOP", "LXDE");
    set_default("SAL_USE_VCLPLUGIN", "gtk");

    /* Ensure display server & protocol bridge are active */
    if (is_installed("/bin/lumen")) {
        char lm_path[] = "/bin/lumen";
        char *lm_args[] = {lm_path, NULL};
        (void)spawn(lm_path, lm_args);
        (void)poll(NULL, 0, 100);
    }
    if (is_installed("/bin/display-protocols")) {
        char dp_path[] = "/bin/display-protocols";
        char dp_opt[] = "--both";
        char *dp_args[] = {dp_path, dp_opt, NULL};
        (void)spawn(dp_path, dp_args);
        (void)poll(NULL, 0, 100);
    }

    /* Resolve official upstream Openbox */
    const char *openbox_bin = is_installed("/usr/local/bin/openbox") ? "/usr/local/bin/openbox" : "/bin/openbox";
    const char *pcmanfm_bin = is_installed("/usr/local/bin/pcmanfm") ? "/usr/local/bin/pcmanfm" : "/bin/pcmanfm";
    const char *lxpanel_bin = is_installed("/usr/local/bin/lxpanel") ? "/usr/local/bin/lxpanel" : "/bin/lxpanel";

    if (!is_installed(openbox_bin)) {
        fprintf(stderr, "startlxde: Openbox window manager is not installed.\n");
        return 1;
    }

    printf("[startlxde] Starting official upstream Openbox (%s)...\n", openbox_bin);
    char ob_path[64];
    strncpy(ob_path, openbox_bin, sizeof(ob_path) - 1);
    char *ob_args[] = {ob_path, NULL};
    pid_t ob_pid = spawn(ob_path, ob_args);
    if (ob_pid < 0) {
        fprintf(stderr, "startlxde: cannot spawn Openbox (errno=%d)\n", errno);
        return 1;
    }

    /* Wait a moment for Openbox to claim the root window */
    (void)poll(NULL, 0, 100);

    /* Launch official upstream PCManFM for desktop management if present */
    pid_t fm_pid = -1;
    if (is_installed(pcmanfm_bin)) {
        printf("[startlxde] Starting official upstream PCManFM desktop (%s)...\n", pcmanfm_bin);
        char fm_path[64];
        strncpy(fm_path, pcmanfm_bin, sizeof(fm_path) - 1);
        char fm_opt[] = "--desktop";
        char *fm_args[] = {fm_path, fm_opt, NULL};
        fm_pid = spawn(fm_path, fm_args);
    }

    /* Launch official upstream LXPanel if present */
    pid_t panel_pid = -1;
    if (is_installed(lxpanel_bin)) {
        printf("[startlxde] Starting official upstream LXPanel (%s)...\n", lxpanel_bin);
        char panel_path[64];
        strncpy(panel_path, lxpanel_bin, sizeof(panel_path) - 1);
        char *panel_args[] = {panel_path, NULL};
        panel_pid = spawn(panel_path, panel_args);
    }

    printf("[startlxde] Official upstream LXDE desktop session is running.\n");

    /* Supervise the Openbox window manager; exiting Openbox ends the session */
    int status = 0;
    (void)waitpid(ob_pid, &status, 0);

    /* Terminate companion daemons on session exit */
    if (fm_pid > 0) (void)kill(fm_pid, SIGTERM);
    if (panel_pid > 0) (void)kill(panel_pid, SIGTERM);

    return WIFEXITED(status) ? WEXITSTATUS(status) : 0;
}
