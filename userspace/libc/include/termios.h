#ifndef AKAOS_LIBC_TERMIOS_H
#define AKAOS_LIBC_TERMIOS_H

#include <akaos/termios.h>

typedef akaos_tcflag_t tcflag_t;
typedef akaos_cc_t cc_t;
#define NCCS AKAOS_NCCS

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_cc[NCCS];
};

#define ICRNL AKAOS_ICRNL
#define OPOST AKAOS_OPOST
#define ONLCR AKAOS_ONLCR
#define ISIG AKAOS_ISIG
#define ICANON AKAOS_ICANON
#define ECHO AKAOS_ECHO
#define ECHOE AKAOS_ECHOE
#define VINTR AKAOS_VINTR
#define VQUIT AKAOS_VQUIT
#define VERASE AKAOS_VERASE
#define VKILL AKAOS_VKILL
#define VEOF AKAOS_VEOF
#define VMIN AKAOS_VMIN
#define VTIME AKAOS_VTIME
#define VSUSP AKAOS_VSUSP

#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
void cfmakeraw(struct termios *termios_p);

#endif
