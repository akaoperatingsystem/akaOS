#ifndef XSCORE_SYSCALL_FRAME_H
#define XSCORE_SYSCALL_FRAME_H

#include <stdint.h>

struct xscore_syscall_frame {
    uint64_t number;
    uint64_t args[6];
    uint64_t instruction_pointer;
};

#endif /* XSCORE_SYSCALL_FRAME_H */
