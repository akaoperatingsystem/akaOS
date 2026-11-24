/* The session of the helloDesktop environment.
 *
 * The session brings up the parts of the desktop in the order they depend on
 * each other:
 *
 *   1. The display server that speaks X11.  The compositor owns the
 *      framebuffer, and the display server publishes the root window of the
 *      screen onto it.
 *   2. The window manager, which draws the frames of the windows.  The
 *      window manager is the unmodified upstream Openbox, and it uses the
 *      helloDesktop theme with the traffic light buttons.
 *   3. The components of the desktop: the global menu bar, the dock and the
 *      file manager that draws the desktop.
 *
 * Every part is the upstream program of helloSystem.  The session only starts
 * them and waits for them.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* The path of the socket of the display server. */
#define X11_DISPLAY_SOCKET "/tmp/.X11-unix/X0"

/* The components of the desktop, in the order they are started. */
static const char *const COMPONENTS[] = {
    "Menu",
    "Dock",
    "Filer",
};
#define COMPONENT_COUNT (sizeof(COMPONENTS) / sizeof(COMPONENTS[0]))

/* Whether the display server already answers on its socket. */
static int display_is_up(void) {
    struct stat info;
    return stat(X11_DISPLAY_SOCKET, &info) == 0;
}

/* Start a program and let it run on its own.
 *
 * The session does not own the parts of the desktop, so a part that ends
 * does not end the session. */
static int start_program(const char *path, char *const argv[]) {
    int pid = spawn(path, argv);
    if (pid < 0) {
        printf("hellodesktop: cannot start %s: %s\n", path, strerror(errno));
        fflush(stdout);
        return -1;
    }
    printf("hellodesktop: started %s\n", path);
    fflush(stdout);
    return pid;
}

/* Wait until the display server accepts connections. */
static int wait_for_display(int attempts) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (display_is_up()) return 1;
        usleep(50000);
    }
    return 0;
}

/* Collect the parts of the desktop that ended.
 *
 * The parts of the desktop run for as long as the session runs, and one of
 * them may end on its own, for example when the user closes the file
 * manager.  The session collects them so that they do not stay behind as
 * unused processes. */
static void collect_ended_parts(void) {
    for (;;) {
        int status = 0;
        int pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) return;
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            printf("hellodesktop: a part of the desktop ended with status %d\n",
                   WEXITSTATUS(status));
        }
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: hellodesktop-session\n"
             "Start the helloDesktop session: display server, window manager,\n"
             "global menu bar, dock and file manager.");
        return 0;
    }
    if (argc != 1) {
        puts("hellodesktop-session: invalid option or extra operand");
        return 1;
    }

    (void)setenv("DISPLAY", ":0", 1);
    (void)setenv("XDG_DATA_DIRS", "/usr/local/share:/usr/share", 1);
    (void)setenv("XDG_CONFIG_DIRS", "/etc/xdg:/usr/local/etc/xdg", 1);
    (void)setenv("XDG_CURRENT_DESKTOP", "helloDesktop", 1);
    (void)setenv("XDG_SESSION_DESKTOP", "helloDesktop", 1);
    /* The system carries no input method, so the client library is told to
     * use the empty one instead of looking for one that is not there. */
    (void)setenv("XMODIFIERS", "@im=none", 1);
    (void)setenv("LANG", "C.UTF-8", 1);
    (void)setenv("LC_ALL", "C.UTF-8", 1);

    /* The compositor owns the framebuffer of the screen. */
    if (access("/bin/lumen", X_OK) == 0) {
        char compositor_path[] = "/bin/lumen";
        char *compositor_argv[] = {compositor_path, NULL};
        (void)start_program(compositor_path, compositor_argv);
        /* The compositor needs a moment before it accepts a surface. */
        usleep(400000);
    } else {
        printf("hellodesktop: the compositor is not installed\n");
        fflush(stdout);
    }

    /* The display server of the screen. */
    if (!display_is_up()) {
        char server_path[] = "/bin/xserver";
        char *server_argv[] = {server_path, "--display", "0", NULL};
        if (start_program(server_path, server_argv) < 0) return 1;
        if (!wait_for_display(100)) {
            printf("hellodesktop: the display server did not answer\n");
            fflush(stdout);
            return 1;
        }
        printf("hellodesktop: the display server is answering\n");
        fflush(stdout);
    } else {
        printf("hellodesktop: the display server is already running\n");
        fflush(stdout);
    }

    /* The window manager, which draws the frames of the windows. */
    if (access("/usr/local/bin/openbox", X_OK) == 0) {
        char manager_path[] = "/usr/local/bin/openbox";
        char *manager_argv[] = {manager_path, NULL};
        (void)start_program(manager_path, manager_argv);
        /* The window manager has to be in place before the clients connect. */
        usleep(400000);
    } else {
        printf("hellodesktop: the window manager is not installed\n");
        fflush(stdout);
    }

    /* The components of the desktop. */
    for (size_t index = 0U; index < COMPONENT_COUNT; ++index) {
        const char *component = COMPONENTS[index];
        char path[64];
        int length = snprintf(path, sizeof(path), "/usr/local/bin/%s", component);
        if (length <= 0 || (size_t)length >= sizeof(path)) continue;
        if (access(path, X_OK) != 0) {
            printf("hellodesktop: %s is not installed\n", component);
            fflush(stdout);
            continue;
        }
        char *component_argv[] = {path, NULL};
        (void)start_program(path, component_argv);
    }

    /* The session holds the display open for as long as it runs. */
    for (;;) {
        poll(NULL, 0, 1000);
        collect_ended_parts();
    }
}
