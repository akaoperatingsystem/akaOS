#ifndef AKAOS_SECCOMP_H
#define AKAOS_SECCOMP_H

#include <stdint.h>
#include <stddef.h>

/* Seccomp operation modes */
#define AKAOS_SECCOMP_MODE_DISABLED 0U
#define AKAOS_SECCOMP_MODE_STRICT   1U
#define AKAOS_SECCOMP_MODE_FILTER   2U

/* Seccomp syscall operations */
#define AKAOS_SECCOMP_SET_MODE_STRICT 0U
#define AKAOS_SECCOMP_SET_MODE_FILTER 1U

/* Seccomp filter action masks and return codes */
#define AKAOS_SECCOMP_RET_ACTION_FULL  0xffff0000U
#define AKAOS_SECCOMP_RET_DATA         0x0000ffffU

#define AKAOS_SECCOMP_RET_KILL_PROCESS 0x80000000U /* Kill the whole process */
#define AKAOS_SECCOMP_RET_KILL_THREAD  0x00000000U /* Kill the calling thread */
#define AKAOS_SECCOMP_RET_KILL         AKAOS_SECCOMP_RET_KILL_THREAD
#define AKAOS_SECCOMP_RET_TRAP         0x00030000U /* Disallow and deliver SIGSYS */
#define AKAOS_SECCOMP_RET_ERRNO        0x00050000U /* Return error number */
#define AKAOS_SECCOMP_RET_TRACE        0x7ff00000U /* Pass to tracer */
#define AKAOS_SECCOMP_RET_LOG          0x7ffc0000U /* Allow after logging */
#define AKAOS_SECCOMP_RET_ALLOW        0x7fff0000U /* Allow system call to execute */

/* Classical BPF instruction layout */
struct akaos_sock_filter {
    uint16_t code; /* Instruction opcode */
    uint8_t  jt;   /* Jump true offset */
    uint8_t  jf;   /* Jump false offset */
    uint32_t k;    /* Generic multi-use field/constant */
};

struct akaos_sock_fprog {
    uint16_t len;
    struct akaos_sock_filter *filter;
};

/* Data passed to Seccomp-BPF filters describing the system call invocation */
struct akaos_seccomp_data {
    int32_t  nr;                    /* System call number */
    uint32_t arch;                  /* Target architecture AUDIT_ARCH_* */
    uint64_t instruction_pointer;   /* CPU instruction pointer */
    uint64_t args[6];               /* System call arguments */
};

/* Classical BPF instruction classes & opcodes */
#define AKAOS_BPF_LD   0x00U
#define AKAOS_BPF_W    0x00U
#define AKAOS_BPF_ABS  0x20U
#define AKAOS_BPF_JMP  0x05U
#define AKAOS_BPF_JEQ  0x10U
#define AKAOS_BPF_JGT  0x20U
#define AKAOS_BPF_JGE  0x30U
#define AKAOS_BPF_K    0x00U
#define AKAOS_BPF_RET  0x06U
#define AKAOS_BPF_ALU  0x04U
#define AKAOS_BPF_AND  0x50U

#endif /* AKAOS_SECCOMP_H */
