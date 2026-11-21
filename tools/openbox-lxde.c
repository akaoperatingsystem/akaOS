#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

int main(int argc, char **argv) {
    int cfd = open("/dev/console", O_WRONLY);
    const char *config = "/etc/xdg/openbox/LXDE/rc.xml";
    if (access(config, F_OK) != 0) config = "/usr/local/etc/xdg/openbox/rc.xml";
    char *new_argv[argc + 4];
    new_argv[0] = "/usr/local/bin/openbox";
    new_argv[1] = "--config-file";
    new_argv[2] = (char *)config;
    for (int i = 1; i < argc; ++i) {
        new_argv[2 + i] = argv[i];
    }
    new_argv[argc + 2] = NULL;
    if (cfd >= 0) {
        char msg[128];
        int len = snprintf(msg, sizeof(msg), "[openbox-lxde] executing %s\n", new_argv[0]);
        if (len > 0) (void)write(cfd, msg, (size_t)len);
    }
    execv("/usr/local/bin/openbox", new_argv);
    if (cfd >= 0) {
        char msg[128];
        int len = snprintf(msg, sizeof(msg), "[openbox-lxde] execv failed: %d (%s)\n", errno, strerror(errno));
        if (len > 0) (void)write(cfd, msg, (size_t)len);
        close(cfd);
    }
    return 127;
}
