/*
 * Seccomp-BPF Syscall Filtering Engine for XSCore
 *
 * Implements:
 * - Strict Seccomp Mode (allows read, write, exit, sigreturn only)
 * - Classical Berkeley Packet Filter (cBPF) bytecode interpreter for syscalls
 * - Action enforcement (ALLOW, ERRNO, TRAP, KILL)
 */

#include <xscore/seccomp.h>
#include <xscore/process.h>
#include <xscore/serial.h>
#include <xscore/heap.h>
#include <xscore/usercopy.h>
#include <akaos/syscall.h>
#include <akaos/errno.h>
#include <stddef.h>
#include <string.h>

void *memcpy(void *dst, const void *src, size_t count);
void *memset(void *dst, int val, size_t count);

#define SECCOMP_MAX_FILTER_LEN 512U
#define BPF_MAX_EXEC_STEPS     1024U

/* Classical BPF interpreter on struct akaos_seccomp_data */
static uint32_t bpf_eval(const struct akaos_sock_filter *filter, uint16_t len,
                         const struct akaos_seccomp_data *data) {
    if (!filter || len == 0 || !data) return AKAOS_SECCOMP_RET_KILL;

    uint32_t A = 0;
    uint32_t X = 0;
    uint32_t M[16];
    memset(M, 0, sizeof(M));

    uint16_t pc = 0;
    uint32_t steps = 0;

    const uint8_t *raw_data = (const uint8_t *)data;
    size_t data_size = sizeof(struct akaos_seccomp_data);

    while (pc < len && steps++ < BPF_MAX_EXEC_STEPS) {
        const struct akaos_sock_filter *inst = &filter[pc++];
        uint16_t code = inst->code;

        switch (code & 0x07) {
        case AKAOS_BPF_LD:
            if ((code & 0x18) == 0x00) { /* BPF_W */
                if ((code & 0xe0) == AKAOS_BPF_ABS) {
                    uint32_t k = inst->k;
                    if (k + 4 <= data_size) {
                        A = *(const uint32_t *)(raw_data + k);
                    } else {
                        return AKAOS_SECCOMP_RET_KILL;
                    }
                }
            }
            break;

        case AKAOS_BPF_ALU:
            if ((code & 0xf0) == AKAOS_BPF_AND) {
                uint32_t val = (code & 0x08) ? X : inst->k;
                A &= val;
            }
            break;

        case AKAOS_BPF_JMP:
            if ((code & 0xf0) == AKAOS_BPF_JEQ) {
                uint32_t val = (code & 0x08) ? X : inst->k;
                if (A == val) {
                    pc = (uint16_t)(pc + inst->jt);
                } else {
                    pc = (uint16_t)(pc + inst->jf);
                }
            } else if ((code & 0xf0) == AKAOS_BPF_JGT) {
                uint32_t val = (code & 0x08) ? X : inst->k;
                if (A > val) {
                    pc = (uint16_t)(pc + inst->jt);
                } else {
                    pc = (uint16_t)(pc + inst->jf);
                }
            } else if ((code & 0xf0) == AKAOS_BPF_JGE) {
                uint32_t val = (code & 0x08) ? X : inst->k;
                if (A >= val) {
                    pc = (uint16_t)(pc + inst->jt);
                } else {
                    pc = (uint16_t)(pc + inst->jf);
                }
            } else if ((code & 0xf0) == 0x00) { /* Unconditional JMP */
                pc = (uint16_t)(pc + inst->k);
            }
            break;

        case AKAOS_BPF_RET:
            return (code & 0x18) ? A : inst->k;

        default:
            return AKAOS_SECCOMP_RET_KILL;
        }
    }

    return AKAOS_SECCOMP_RET_KILL;
}

uint32_t xscore_seccomp_evaluate(struct xscore_process *process,
                                 uint32_t syscall_number,
                                 const uint64_t args[6],
                                 uint64_t instruction_pointer) {
    if (!process) return AKAOS_SECCOMP_RET_ALLOW;

    uint32_t mode = xscore_process_get_seccomp_mode(process);
    if (mode == AKAOS_SECCOMP_MODE_DISABLED) {
        return AKAOS_SECCOMP_RET_ALLOW;
    }

    if (mode == AKAOS_SECCOMP_MODE_STRICT) {
        /* Allow only read, write, exit, and sigreturn */
        if (syscall_number == AKAOS_SYSCALL_READ ||
            syscall_number == AKAOS_SYSCALL_WRITE ||
            syscall_number == AKAOS_SYSCALL_EXIT ||
            syscall_number == AKAOS_SYSCALL_SIGRETURN) {
            return AKAOS_SECCOMP_RET_ALLOW;
        }
        xscore_serial_write("[SECCOMP] Strict mode violation: syscall denied\n");
        return AKAOS_SECCOMP_RET_KILL;
    }

    if (mode == AKAOS_SECCOMP_MODE_FILTER) {
        uint16_t flen = 0;
        const struct akaos_sock_filter *filt = xscore_process_get_seccomp_filter(process, &flen);
        if (!filt || flen == 0) return AKAOS_SECCOMP_RET_KILL;

        struct akaos_seccomp_data sc_data;
        sc_data.nr = (int32_t)syscall_number;
#if defined(__x86_64__)
        sc_data.arch = 0xc000003eU; /* AUDIT_ARCH_X86_64 */
#elif defined(__i386__)
        sc_data.arch = 0x40000003U; /* AUDIT_ARCH_I386 */
#elif defined(__aarch64__)
        sc_data.arch = 0xc00000b7U; /* AUDIT_ARCH_AARCH64 */
#elif defined(__arm__)
        sc_data.arch = 0x40000028U; /* AUDIT_ARCH_ARM */
#elif defined(__riscv) && __riscv_xlen == 64
        sc_data.arch = 0xc00000f3U; /* AUDIT_ARCH_RISCV64 */
#elif defined(__riscv) && __riscv_xlen == 32
        sc_data.arch = 0x400000f3U; /* AUDIT_ARCH_RISCV32 */
#else
        sc_data.arch = 0U;
#endif
        sc_data.instruction_pointer = instruction_pointer;
        if (args) {
            for (int i = 0; i < 6; ++i) sc_data.args[i] = args[i];
        } else {
            memset(sc_data.args, 0, sizeof(sc_data.args));
        }

        return bpf_eval(filt, flen, &sc_data);
    }

    return AKAOS_SECCOMP_RET_ALLOW;
}

int xscore_seccomp_set_mode_strict(struct xscore_process *process) {
    if (!process) return -AKAOS_EINVAL;
    return xscore_process_set_seccomp_strict(process);
}

int xscore_seccomp_set_mode_filter(struct xscore_process *process,
                                   const struct akaos_sock_fprog *fprog) {
    if (!process || !fprog || fprog->len == 0 || fprog->len > SECCOMP_MAX_FILTER_LEN) {
        return -AKAOS_EINVAL;
    }

    size_t alloc_bytes = fprog->len * sizeof(struct akaos_sock_filter);
    struct akaos_sock_filter *kfilt = xscore_heap_allocate(alloc_bytes);
    if (!kfilt) return -AKAOS_ENOMEM;

    if (!xscore_copy_from_user(kfilt, (uint64_t)(uintptr_t)fprog->filter, alloc_bytes)) {
        xscore_heap_free(kfilt);
        return -AKAOS_EFAULT;
    }

    int ret = xscore_process_set_seccomp_filter(process, kfilt, fprog->len);
    if (ret != 0) {
        xscore_heap_free(kfilt);
        return ret;
    }

    xscore_serial_write("[SECCOMP] Installed BPF syscall filter\n");
    return 0;
}
