#ifndef AKAOS_LIBC_SPAWN_H
#define AKAOS_LIBC_SPAWN_H

#include <sys/types.h>
#include <akaos/syscall.h>
#include <signal.h>

#define POSIX_SPAWN_RESETIDS AKAOS_POSIX_SPAWN_RESETIDS
#define POSIX_SPAWN_SETPGROUP AKAOS_POSIX_SPAWN_SETPGROUP
#define POSIX_SPAWN_SETSIGDEF AKAOS_POSIX_SPAWN_SETSIGDEF
#define POSIX_SPAWN_SETSIGMASK AKAOS_POSIX_SPAWN_SETSIGMASK

typedef struct {
    unsigned int count;
    struct akaos_spawn_file_action actions[8];
} posix_spawn_file_actions_t;
typedef struct {
    short flags;
    pid_t pgroup;
    sigset_t sigmask;
    sigset_t sigdefault;
} posix_spawnattr_t;

extern char **environ;

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *actions);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *actions);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *actions,
                                      int descriptor);
/* GNU-compatible extension: close every inherited descriptor >= descriptor
 * in the new process before it enters userspace. */
int posix_spawn_file_actions_addclosefrom_np(
    posix_spawn_file_actions_t *actions, int descriptor);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *actions,
                                     int old_descriptor, int new_descriptor);
int posix_spawnattr_init(posix_spawnattr_t *attributes);
int posix_spawnattr_destroy(posix_spawnattr_t *attributes);
int posix_spawnattr_getflags(const posix_spawnattr_t *attributes,
                             short *flags);
int posix_spawnattr_setflags(posix_spawnattr_t *attributes, short flags);
int posix_spawnattr_getpgroup(const posix_spawnattr_t *attributes,
                              pid_t *process_group);
int posix_spawnattr_setpgroup(posix_spawnattr_t *attributes,
                              pid_t process_group);
int posix_spawnattr_getsigmask(const posix_spawnattr_t *attributes,
                               sigset_t *signal_mask);
int posix_spawnattr_setsigmask(posix_spawnattr_t *attributes,
                               const sigset_t *signal_mask);
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attributes,
                                  sigset_t *signal_default);
int posix_spawnattr_setsigdefault(posix_spawnattr_t *attributes,
                                  const sigset_t *signal_default);

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attributes,
                char *const argv[], char *const envp[]);
int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attributes,
                 char *const argv[], char *const envp[]);

#endif
