#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static void show(const struct termios *settings) {
    struct winsize size;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &size) == 0) {
        printf("speed 0 baud; rows %u; columns %u;\n",
               (unsigned)size.ws_row, (unsigned)size.ws_col);
    }
    printf("%s %s %s %s\n",
           (settings->c_lflag & ISIG) ? "isig" : "-isig",
           (settings->c_lflag & ICANON) ? "icanon" : "-icanon",
           (settings->c_lflag & ECHO) ? "echo" : "-echo",
           (settings->c_iflag & ICRNL) ? "icrnl" : "-icrnl");
}

static void sane(struct termios *settings) {
    settings->c_iflag |= ICRNL;
    settings->c_oflag |= OPOST | ONLCR;
    settings->c_lflag |= ISIG | ICANON | ECHO | ECHOE;
    settings->c_cc[VINTR] = 3;
    settings->c_cc[VERASE] = 0x7f;
    settings->c_cc[VEOF] = 4;
    settings->c_cc[VSUSP] = 26;
    settings->c_cc[VMIN] = 1;
    settings->c_cc[VTIME] = 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("Usage: stty [-a|sane|raw|-raw|echo|-echo|icanon|-icanon|isig|-isig]\nChange or print terminal settings.");
        return 0;
    }
    struct termios settings;
    if (tcgetattr(STDIN_FILENO, &settings) != 0) {
        puts("stty: standard input: Inappropriate ioctl for device");
        return 1;
    }
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "-a") == 0)) {
        show(&settings);
        return 0;
    }
    for (int index = 1; index < argc; ++index) {
        const char *option = argv[index];
        if (strcmp(option, "sane") == 0 || strcmp(option, "-raw") == 0) sane(&settings);
        else if (strcmp(option, "raw") == 0) cfmakeraw(&settings);
        else if (strcmp(option, "echo") == 0) settings.c_lflag |= ECHO;
        else if (strcmp(option, "-echo") == 0) settings.c_lflag &= ~ECHO;
        else if (strcmp(option, "icanon") == 0) settings.c_lflag |= ICANON;
        else if (strcmp(option, "-icanon") == 0) settings.c_lflag &= ~ICANON;
        else if (strcmp(option, "isig") == 0) settings.c_lflag |= ISIG;
        else if (strcmp(option, "-isig") == 0) settings.c_lflag &= ~ISIG;
        else {
            printf("stty: invalid argument '%s'\n", option);
            return 1;
        }
    }
    return tcsetattr(STDIN_FILENO, TCSANOW, &settings) == 0 ? 0 : 1;
}
