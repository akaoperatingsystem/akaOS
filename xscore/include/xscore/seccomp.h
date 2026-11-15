#ifndef XSCORE_SECCOMP_H
#define XSCORE_SECCOMP_H

#include <stdint.h>
#include <stddef.h>
#include <akaos/seccomp.h>

struct xscore_process;

/* Seccomp-BPF Kernel Engine */
uint32_t xscore_seccomp_evaluate(struct xscore_process *process,
                                 uint32_t syscall_number,
                                 const uint64_t args[6],
                                 uint64_t instruction_pointer);

int xscore_seccomp_set_mode_strict(struct xscore_process *process);
int xscore_seccomp_set_mode_filter(struct xscore_process *process,
                                   const struct akaos_sock_fprog *fprog);

#endif /* XSCORE_SECCOMP_H */
