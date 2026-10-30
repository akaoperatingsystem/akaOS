#ifndef XSCORE_PROCESS_H
#define XSCORE_PROCESS_H

#include <stddef.h>
#include <stdint.h>
#include <akaos/fs.h>

struct xscore_process;
struct xscore_fd_table;
struct akaos_mmap_args;
struct akaos_spawn_file_action;

enum xscore_process_state {
    XSCORE_PROCESS_UNUSED = 0,
    /* Reserved table slot. Never expose a partially initialized process. */
    XSCORE_PROCESS_ALLOCATING,
    XSCORE_PROCESS_READY,
    XSCORE_PROCESS_RUNNING,
    XSCORE_PROCESS_STOPPED,
    XSCORE_PROCESS_EXITED,
    XSCORE_PROCESS_REAPED,
};

int xscore_process_initialize(void);
struct xscore_process *xscore_process_create_elf(const uint8_t *image,
                                                 size_t image_size);
struct xscore_process *xscore_process_create_elf_args(const uint8_t *image,
                                                      size_t image_size,
                                                      int argc,
                                                      const char *const argv[]);
struct xscore_process *xscore_process_create_path(const char *path);
struct xscore_process *xscore_process_create_path_args(const char *path,
                                                       int argc,
                                                       const char *const argv[]);
struct xscore_process *xscore_process_create_path_args_at(uint32_t cwd_node,
                                                          const char *path,
                                                          int argc,
                                                          const char *const argv[]);
struct xscore_process *xscore_process_create_path_args_on_cpu_at(
    uint32_t cwd_node, const char *path, int argc,
    const char *const argv[], uint32_t cpu_index);
int xscore_process_wait(struct xscore_process *process, uint64_t *exit_status);
int xscore_process_reap(struct xscore_process *process);
/* Register the kernel-launched init/supervisor as the orphan reaper. */
int xscore_process_set_reaper(struct xscore_process *process);

uint64_t xscore_process_pid(const struct xscore_process *process);
uint64_t xscore_process_parent_pid(const struct xscore_process *process);
uint64_t xscore_process_pgid(const struct xscore_process *process);
uint64_t xscore_process_sid(const struct xscore_process *process);
uint64_t xscore_process_thread_id(const struct xscore_process *process);
uint64_t xscore_process_address_space_root(const struct xscore_process *process);
int xscore_process_page_info(const struct xscore_process *process,
                             uint64_t virtual_address, uint64_t *flags,
                             uint64_t *physical_address);
enum xscore_process_state xscore_process_state(const struct xscore_process *process);
int xscore_process_has_descriptor(const struct xscore_process *process,
                                  int descriptor);
int xscore_process_wait_did_block(const struct xscore_process *process);

struct xscore_process *xscore_process_current(void);
struct xscore_fd_table *xscore_process_fd_table(struct xscore_process *process);
void xscore_process_user_thread_entry(struct xscore_process *process,
                                      uint64_t entry, uint64_t stack,
                                      uint64_t user_arg0);
/* Enable AP placement for ordinary interactive descendants after boot tests. */
void xscore_process_enable_multicore_userspace(int enabled);
const char *xscore_process_cwd(const struct xscore_process *process);
size_t xscore_process_copy_cwd(const struct xscore_process *process,
                               char *buffer, size_t capacity);
uint32_t xscore_process_cwd_node(const struct xscore_process *process);
int xscore_process_set_cwd(struct xscore_process *process, const char *path);
int xscore_process_set_cwd_node(struct xscore_process *process,
                                uint32_t node_id);
const char *xscore_process_get_name(const struct xscore_process *process);
void xscore_process_set_name(struct xscore_process *process, const char *name);
uint32_t xscore_process_uid(const struct xscore_process *process);
uint32_t xscore_process_gid(const struct xscore_process *process);
uint32_t xscore_process_effective_uid(const struct xscore_process *process);
uint32_t xscore_process_effective_gid(const struct xscore_process *process);
int xscore_process_set_uid(struct xscore_process *process, uint32_t uid);
int xscore_process_set_gid(struct xscore_process *process, uint32_t gid);
int xscore_process_setpgid(uint64_t pid, uint64_t pgid);
uint64_t xscore_process_getpgid(uint64_t pid);
int64_t xscore_process_setsid(void);
int64_t xscore_process_getsid(uint64_t pid);
int xscore_process_tcsetpgrp(uint64_t pgid);
uint64_t xscore_process_tcgetpgrp(void);
size_t xscore_process_proc_status(char *buffer, size_t capacity);
size_t xscore_process_proc_status_pid(uint64_t pid, char *buffer,
                                      size_t capacity);
size_t xscore_process_proc_list(char *buffer, size_t capacity);
size_t xscore_process_get_active_pids(uint64_t *pids_out, size_t max_count);
int xscore_process_pid_info(uint64_t pid, uint32_t *uid, uint32_t *gid,
                            enum xscore_process_state *state);

int64_t xscore_process_spawn_path(const char *path, int argc,
                                 const char *const argv[], uint64_t *out_pid);
int64_t xscore_process_spawn_path_env(const char *path, int argc,
                                      const char *const argv[], int envc,
                                      const char *const envp[],
                                      const struct akaos_spawn_file_action *actions,
                                      uint32_t action_count,
                                      uint32_t spawn_flags,
                                      uint32_t process_group,
                                      uint32_t signal_mask,
                                      uint32_t signal_default,
                                      uint64_t *out_pid);
int64_t xscore_process_execve(const char *path, int argc,
                              const char *const argv[], int envc,
                              const char *const envp[]);
int64_t xscore_process_mmap(const struct akaos_mmap_args *arguments);
int64_t xscore_process_munmap(uint64_t address, uint64_t length);
int64_t xscore_process_mprotect(uint64_t address, uint64_t length,
                                uint64_t protection);
int64_t xscore_process_shm_open(const char *name, uint64_t flags,
                                uint32_t mode);
int64_t xscore_process_memfd_create(const char *name, uint32_t name_length,
                                   uint32_t flags);
int64_t xscore_process_shm_fcntl(int descriptor, int command,
                                 uint64_t argument);
int64_t xscore_process_shm_unlink(const char *name);
int64_t xscore_process_ftruncate(int descriptor, uint64_t length);
int64_t xscore_process_shm_size(uint32_t object_id);
int64_t xscore_process_shm_stat(uint32_t object_id,
                                struct akaos_stat *status);
int64_t xscore_process_shm_read(uint32_t object_id, uint64_t offset,
                                void *buffer, uint64_t length);
int64_t xscore_process_shm_write(uint32_t object_id, uint64_t offset,
                                 const void *buffer, uint64_t length);
void xscore_process_shm_descriptor_release(uint32_t object_id);
int64_t xscore_process_fork(uint64_t *out_pid);
void xscore_process_capture_syscall_context(uint32_t number,
                                            const void *frame,
                                            uint64_t elr_el1,
                                            uint64_t sp_el0,
                                            uint64_t spsr_el1);
int64_t xscore_process_waitpid(uint64_t pid, uint64_t *out_status);
int64_t xscore_process_waitpid_options(uint64_t pid, uint64_t *out_status, uint32_t options);
int64_t xscore_process_signal(uint64_t pid, uint32_t signal);
int xscore_process_signal_foreground(uint32_t signal);
int xscore_process_signal_pgrp(uint64_t pgid, uint32_t signal);
int xscore_process_set_signal_ignored(struct xscore_process *process,
                                      uint32_t signal, int ignored);
struct akaos_sigaction;
int64_t xscore_process_sigaction(uint32_t signal,
                                 const struct akaos_sigaction *act,
                                 struct akaos_sigaction *oldact);
int64_t xscore_process_update_signal_mask(uint32_t how, uint32_t mask,
                                          uint32_t *old_mask);
int64_t xscore_process_sigreturn(void);
int xscore_process_dispatch_user_signal(void *trap_frame);
int xscore_process_sigreturn_restore(void *trap_frame);
int xscore_process_signal_pending_current(void);
void xscore_process_record_termination_signal(uint32_t signal);
uint32_t xscore_process_pending_signal(const struct xscore_process *process);
int xscore_process_take_pending_signal(uint32_t *signal);
int xscore_process_signalfd_pending(uint32_t signal_mask);
int xscore_process_signalfd_take(uint32_t signal_mask, uint32_t *signal,
                                 uint64_t *sender_pid, uint32_t *sender_uid);
int xscore_process_deliver_pending_signal(uint64_t *irq_frame);
int xscore_process_deliver_thread_exit(uint64_t *irq_frame);
uint64_t xscore_process_brk(struct xscore_process *process, uint64_t new_brk);
#define XSCORE_USER_FAULT_WRITE UINT32_C(0x01)
#define XSCORE_USER_FAULT_PRESENT UINT32_C(0x02)
int xscore_process_handle_page_fault(uint64_t fault_address,
                                     uint32_t fault_flags);
void xscore_process_report_name(void);
void xscore_process_report_fault(uint64_t fault_address);

#endif
