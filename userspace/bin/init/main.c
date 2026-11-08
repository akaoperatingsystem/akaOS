#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>

static int write_str(int fd, const char *str) {
    size_t len = strlen(str);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, str + written, len - written);
        if (res <= 0) return 0;
        written += (size_t)res;
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        write_str(STDOUT_FILENO, "Usage: init\nakaOS process supervisor and service manager (PID 1).\n");
        return 0;
    }

    (void)chown("/home/user", 1000, 1000);
    (void)chmod("/home/user", 0755);

    /* Detect available RAM from /proc/meminfo. On <= 32MB boots, defer background
     * daemons to preserve maximum resident memory for login and shell utilities. */
    int low_ram_mode = 0;
    int mem_fd = open("/proc/meminfo", 0 /* O_RDONLY */);
    if (mem_fd >= 0) {
        char mbuf[256];
        ssize_t mn = read(mem_fd, mbuf, sizeof(mbuf) - 1);
        close(mem_fd);
        if (mn > 0) {
            mbuf[mn] = '\0';
            const char *mp = strstr(mbuf, "MemTotal:");
            if (mp) {
                mp += 9;
                while (*mp == ' ' || *mp == '\t') mp++;
                unsigned long kb = 0;
                while (*mp >= '0' && *mp <= '9') {
                    kb = kb * 10 + (*mp - '0');
                    mp++;
                }
                if (kb > 0 && kb <= 32768) {
                    low_ram_mode = 1;
                }
            }
        }
    }

    char system_bus_path[20] = "/bin/dbus-daemon";
    if (!low_ram_mode) {
        char system_bus_mode[10] = "--system";
        char *system_bus_argv[3] = {system_bus_path, system_bus_mode, NULL};
        if (spawn(system_bus_path, system_bus_argv) < 0) {
            write_str(STDERR_FILENO, "init: unable to start system D-Bus bus\n");
        }

        char session_bus_mode[10] = "--session";
        char *session_bus_argv[3] = {system_bus_path, session_bus_mode, NULL};
        if (spawn(system_bus_path, session_bus_argv) < 0) {
            write_str(STDERR_FILENO, "init: unable to start session D-Bus bus\n");
        }
    } else {
        write_str(STDOUT_FILENO, "init: low-RAM mode active (<= 32MB detected): deferring background daemons\n");
    }

    char login_path[16] = "/bin/login";
    char *login_argv[2];
    login_argv[0] = login_path;
    login_argv[1] = NULL;

    for (;;) {
        int pid = spawn(login_path, login_argv);
        if (pid < 0) {
            write_str(STDERR_FILENO, "init: unable to spawn /bin/login (errno=");
            int e = errno;
            if (e == 0) {
                write_str(STDERR_FILENO, "0");
            } else {
                char buf[16];
                int bidx = 0;
                int tmp = e;
                while (tmp > 0) {
                    buf[bidx++] = '0' + (tmp % 10);
                    tmp /= 10;
                }
                while (bidx > 0) {
                    char c = buf[--bidx];
                    write(STDERR_FILENO, &c, 1);
                }
            }
            write_str(STDERR_FILENO, ")\n");
            for (;;) {
#if defined(__x86_64__) || defined(__i386__)
                __asm__ volatile("pause");
#elif defined(__aarch64__) || defined(__arm__)
                __asm__ volatile("wfe");
#elif defined(__riscv)
                __asm__ volatile("wfi");
#else
                __asm__ volatile("");
#endif
            }
        }

        int status = 0;
        for (;;) {
            int waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) break;
            if (waited < 0) break;

            /* Polling the login child lets init reap unrelated adopted
             * zombies throughout a long-lived session.  waitpid(-1) can
             * select the login process too, so recognize that PID here. */
            int orphaned_status = 0;
            int orphaned_pid;
            while ((orphaned_pid = waitpid(-1, &orphaned_status,
                                            WNOHANG)) > 0) {
                if (orphaned_pid == pid) break;
            }
            if (orphaned_pid == pid) break;
            (void)usleep(100000);
        }

        write_str(STDOUT_FILENO, "\n");
    }

    return 0;
}
