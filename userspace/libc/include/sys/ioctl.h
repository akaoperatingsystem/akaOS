#ifndef AKAOS_LIBC_SYS_IOCTL_H
#define AKAOS_LIBC_SYS_IOCTL_H

#include <akaos/termios.h>
#include <akaos/net.h>
#include <akaos/ioctl.h>

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#define TCGETS AKAOS_TCGETS
#define TCSETS AKAOS_TCSETS
#define TIOCGWINSZ AKAOS_TIOCGWINSZ
#define TIOCSWINSZ AKAOS_TIOCSWINSZ
#define TIOCGNAME AKAOS_TIOCGNAME
#define SIOCGIFNAME 0x8910
#define SIOCGIFCONF 0x8912
#define SIOCGIFFLAGS 0x8913
#define SIOCGIFADDR 0x8915
#define SIOCGIFBRDADDR 0x8919
#define SIOCGIFNETMASK 0x891b
#define SIOCGIFMTU 0x8921
#define SIOCGIFHWADDR 0x8927
#define SIOCGIFINDEX 0x8933
#define SIOCGIFSTATS AKAOS_SIOCGIFSTATS
#define FIONREAD AKAOS_FIONREAD

#ifdef __cplusplus
extern "C" {
#endif
int ioctl(int fd, unsigned long request, void *argument);
#ifdef __cplusplus
}
#endif

#endif
