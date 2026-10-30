#ifndef AKAOS_TERMIOS_H
#define AKAOS_TERMIOS_H

#include <stdint.h>

typedef uint32_t akaos_tcflag_t;
typedef uint8_t akaos_cc_t;

#define AKAOS_NCCS 8

struct akaos_termios {
    akaos_tcflag_t c_iflag;
    akaos_tcflag_t c_oflag;
    akaos_tcflag_t c_cflag;
    akaos_tcflag_t c_lflag;
    akaos_cc_t c_cc[AKAOS_NCCS];
};

struct akaos_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

#define AKAOS_ICRNL UINT32_C(0x0001)
#define AKAOS_OPOST UINT32_C(0x0001)
#define AKAOS_ONLCR UINT32_C(0x0002)
#define AKAOS_ISIG UINT32_C(0x0001)
#define AKAOS_ICANON UINT32_C(0x0002)
#define AKAOS_ECHO UINT32_C(0x0004)
#define AKAOS_ECHOE UINT32_C(0x0008)

#define AKAOS_VINTR 0
#define AKAOS_VQUIT 1
#define AKAOS_VERASE 2
#define AKAOS_VKILL 3
#define AKAOS_VEOF 4
#define AKAOS_VMIN 5
#define AKAOS_VTIME 6
#define AKAOS_VSUSP 7

#define AKAOS_TCGETS UINT32_C(0x5401)
#define AKAOS_TCSETS UINT32_C(0x5402)
#define AKAOS_TIOCGWINSZ UINT32_C(0x5413)
#define AKAOS_TIOCSWINSZ UINT32_C(0x5414)
#define AKAOS_TIOCGNAME UINT32_C(0x5480)
#define AKAOS_TTY_NAME_MAX 32

#endif
