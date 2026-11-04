#define AKAOS_LIBC_SYS_STAT_IMPLEMENTATION 1
#include <akaos/syscall.h>
#include <akaos/futex.h>
#include <akaos/fs.h>
#include <akaos/signal.h>
#include <signal.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <resolv.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <spawn.h>
#include <time.h>
#include <akaos/net.h>
#include <sys/mount.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include "pthread_internal.h"


/* libc-only descriptor bridges layered over the native bounded descriptor
 * ABI.  They return 1 when a virtual descriptor handled the operation. */
extern int __akaos_eventfd_read(int descriptor, void *buffer, size_t length,
                                ssize_t *result);
extern int __akaos_eventfd_write(int descriptor, const void *buffer,
                                 size_t length, ssize_t *result);
extern int __akaos_eventfd_close(int descriptor);
extern int __akaos_eventfd_poll_fd(int descriptor, short events);
extern int __akaos_eventfd_fcntl(int descriptor, int command,
                                 uint64_t argument, int *result);
extern int __akaos_timerfd_read(int descriptor, void *buffer, size_t length,
                                ssize_t *result);
extern int __akaos_timerfd_write(int descriptor, const void *buffer,
                                 size_t length, ssize_t *result);
extern int __akaos_timerfd_close(int descriptor);
extern int __akaos_timerfd_poll_fd(int descriptor, short events);
extern void __akaos_timerfd_shutdown(void);

char **environ;

#define AKAOS_ENVIRONMENT_CAPACITY 64U
static char *owned_environment[AKAOS_ENVIRONMENT_CAPACITY];
static size_t owned_environment_count;
static int environment_is_owned;

static int spawn_with_environment(const char *path, char *const argv[],
                                  char *const envp[],
                                  const posix_spawn_file_actions_t *actions,
                                  const posix_spawnattr_t *attributes);
int __akaos_epoll_close(int descriptor);

void __akaos_libc_set_environ(int argc, char **argv,
                              uintptr_t initial_stack_pointer) {
    akaos_pthread_set_initial_stack(initial_stack_pointer);
    environ = argv ? argv + argc + 1 : NULL;
    if (!environ) return;

    /* Directly loaded executables own a PT_TLS image that must be installed
     * before main.  An ELF interpreter receives AT_BASE != 0 and initializes
     * the target image through dlopen instead, so do not treat its AT_PHDR as
     * the interpreter's own program headers. */
    char **environment = environ;
    while (*environment) ++environment;
    uintptr_t *auxiliary = (uintptr_t *)(void *)(environment + 1);
    uintptr_t interpreter_base = 0;
    const char *executable_path = NULL;
    uintptr_t interpreter_present = 0;
    uintptr_t program_headers = 0;
    uintptr_t program_header_count = 0;
    uintptr_t program_header_entry_size = 0;
    for (size_t index = 0; index < 64U; ++index) {
        uintptr_t type = auxiliary[index * 2U];
        uintptr_t value = auxiliary[index * 2U + 1U];
        if (type == 0U) break;
        if (type == 7U) interpreter_base = value;       /* AT_BASE */
        else if (type == 0x6f6b0001U) interpreter_present = value;
        else if (type == 3U) program_headers = value;   /* AT_PHDR */
        else if (type == 4U) program_header_entry_size = value; /* AT_PHENT */
        else if (type == 5U) program_header_count = value; /* AT_PHNUM */
        else if (type == 31U)                           /* AT_EXECFN */
            executable_path = (const char *)value;
    }
    if (interpreter_base == 0U && interpreter_present == 0U &&
        executable_path && *executable_path &&
        !akaos_dl_main_tls_initialize(executable_path, program_headers,
                                      program_header_count,
                                      program_header_entry_size)) {
        static const char failure[] =
            "akaOS: unable to initialize executable TLS\n";
        (void)write(STDERR_FILENO, failure, sizeof(failure) - 1U);
        _exit(127);
    }
}

char *getenv(const char *name) {
    if (!name || !*name || !environ) return NULL;
    size_t len = strlen(name);
    for (char **ep = environ; *ep; ++ep) {
        if (strncmp(*ep, name, len) == 0 && (*ep)[len] == '=') {
            return *ep + len + 1;
        }
    }
    return NULL;
}

static int environment_name_valid(const char *name) {
    return name && name[0] != '\0' && strchr(name, '=') == NULL;
}

static int environment_name_matches(const char *entry, const char *name,
                                    size_t name_length) {
    return entry && strncmp(entry, name, name_length) == 0 &&
           entry[name_length] == '=';
}

static int ensure_owned_environment(void) {
    if (environment_is_owned && environ == owned_environment) return 1;

    /* Startup code or an application may repoint environ after libc owns a
     * previous vector. Snapshot the currently active vector before freeing
     * the old owned strings or using its recorded count. */
    char *replacement_environment[AKAOS_ENVIRONMENT_CAPACITY];
    size_t copied = 0;
    if (environ) {
        for (char **entry = environ; *entry; ++entry) {
            if (copied + 1U >= AKAOS_ENVIRONMENT_CAPACITY) {
                errno = ENOMEM;
                goto fail;
            }
            size_t length = strlen(*entry);
            replacement_environment[copied] = malloc(length + 1U);
            if (!replacement_environment[copied]) goto fail;
            memcpy(replacement_environment[copied], *entry, length + 1U);
            ++copied;
        }
    }

    if (environment_is_owned) {
        for (size_t index = 0; index < owned_environment_count; ++index)
            free(owned_environment[index]);
    }
    for (size_t index = 0; index < copied; ++index)
        owned_environment[index] = replacement_environment[index];
    owned_environment[copied] = NULL;
    owned_environment_count = copied;
    environment_is_owned = 1;
    environ = owned_environment;
    return 1;
fail:
    while (copied > 0) free(replacement_environment[--copied]);
    errno = ENOMEM;
    return 0;
}

static size_t find_environment_entry(const char *name, size_t name_length) {
    if (!environ) return (size_t)-1;
    for (size_t index = 0; index < owned_environment_count; ++index) {
        if (environment_name_matches(environ[index], name, name_length))
            return index;
    }
    return (size_t)-1;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!environment_name_valid(name) || !value) {
        errno = EINVAL;
        return -1;
    }
    size_t name_length = strlen(name);
    size_t value_length = strlen(value);
    if (name_length > (size_t)-1 - value_length - 2U) {
        errno = ENOMEM;
        return -1;
    }
    if (!environment_is_owned) {
        size_t existing = find_environment_entry(name, name_length);
        if (existing != (size_t)-1 && !overwrite) return 0;
    }
    if (!ensure_owned_environment()) return -1;
    size_t existing = find_environment_entry(name, name_length);
    if (existing != (size_t)-1 && !overwrite) return 0;
    char *entry = malloc(name_length + value_length + 2U);
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(entry, name, name_length);
    entry[name_length] = '=';
    memcpy(entry + name_length + 1U, value, value_length + 1U);
    if (existing != (size_t)-1) {
        free(owned_environment[existing]);
        owned_environment[existing] = entry;
        return 0;
    }
    if (owned_environment_count + 1U >= AKAOS_ENVIRONMENT_CAPACITY) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }
    owned_environment[owned_environment_count++] = entry;
    owned_environment[owned_environment_count] = NULL;
    return 0;
}

int unsetenv(const char *name) {
    if (!environment_name_valid(name)) {
        errno = EINVAL;
        return -1;
    }
    if (!ensure_owned_environment()) return -1;
    size_t name_length = strlen(name);
    size_t existing = find_environment_entry(name, name_length);
    if (existing == (size_t)-1) return 0;
    free(owned_environment[existing]);
    for (size_t index = existing; index + 1U < owned_environment_count;
         ++index)
        owned_environment[index] = owned_environment[index + 1U];
    --owned_environment_count;
    owned_environment[owned_environment_count] = NULL;
    return 0;
}

int putenv(char *string) {
    if (!string || string[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    char *separator = strchr(string, '=');
    if (!separator || separator == string) {
        errno = EINVAL;
        return -1;
    }
    size_t name_length = (size_t)(separator - string);
    char name[128];
    if (name_length >= sizeof(name)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(name, string, name_length);
    name[name_length] = '\0';
    return setenv(name, separator + 1U, 1);
}

int clearenv(void) {
    if (!ensure_owned_environment()) return -1;
    for (size_t index = 0; index < owned_environment_count; ++index)
        free(owned_environment[index]);
    owned_environment_count = 0;
    owned_environment[0] = NULL;
    return 0;
}

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *actions) {
    if (!actions) return EINVAL;
    memset(actions, 0, sizeof(*actions));
    return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *actions) {
    if (!actions) return EINVAL;
    memset(actions, 0, sizeof(*actions));
    return 0;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *actions,
                                      int descriptor) {
    if (!actions || descriptor < 0 || descriptor >= 16) return EINVAL;
    if (actions->count >= 8U) return E2BIG;
    struct akaos_spawn_file_action *action =
        &actions->actions[actions->count++];
    memset(action, 0, sizeof(*action));
    action->type = AKAOS_SPAWN_ACTION_CLOSE;
    action->first = descriptor;
    return 0;
}

int posix_spawn_file_actions_addclosefrom_np(
    posix_spawn_file_actions_t *actions, int descriptor) {
    if (!actions || descriptor < 0 || descriptor >= 32) return EINVAL;
    if (actions->count >= 8U) return E2BIG;
    struct akaos_spawn_file_action *action =
        &actions->actions[actions->count++];
    memset(action, 0, sizeof(*action));
    action->type = AKAOS_SPAWN_ACTION_CLOSEFROM;
    action->first = descriptor;
    return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *actions,
                                     int old_descriptor, int new_descriptor) {
    if (!actions || old_descriptor < 0 || old_descriptor >= 16 ||
        new_descriptor < 0 || new_descriptor >= 16) return EINVAL;
    if (actions->count >= 8U) return E2BIG;
    struct akaos_spawn_file_action *action =
        &actions->actions[actions->count++];
    memset(action, 0, sizeof(*action));
    action->type = AKAOS_SPAWN_ACTION_DUP2;
    action->first = old_descriptor;
    action->second = new_descriptor;
    return 0;
}

int posix_spawnattr_init(posix_spawnattr_t *attributes) {
    if (!attributes) return EINVAL;
    memset(attributes, 0, sizeof(*attributes));
    return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t *attributes) {
    return attributes ? 0 : EINVAL;
}

int posix_spawnattr_getflags(const posix_spawnattr_t *attributes,
                             short *flags) {
    if (!attributes || !flags) return EINVAL;
    *flags = attributes->flags;
    return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t *attributes, short flags) {
    const unsigned short supported = POSIX_SPAWN_RESETIDS |
        POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF |
        POSIX_SPAWN_SETSIGMASK;
    if (!attributes || flags < 0 ||
        ((unsigned short)flags & (unsigned short)~supported) != 0U)
        return EINVAL;
    attributes->flags = flags;
    return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t *attributes,
                              pid_t *process_group) {
    if (!attributes || !process_group) return EINVAL;
    *process_group = attributes->pgroup;
    return 0;
}

int posix_spawnattr_setpgroup(posix_spawnattr_t *attributes,
                              pid_t process_group) {
    if (!attributes || process_group < 0) return EINVAL;
    attributes->pgroup = process_group;
    return 0;
}

int posix_spawnattr_getsigmask(const posix_spawnattr_t *attributes,
                               sigset_t *signal_mask) {
    if (!attributes || !signal_mask) return EINVAL;
    *signal_mask = attributes->sigmask;
    return 0;
}

int posix_spawnattr_setsigmask(posix_spawnattr_t *attributes,
                               const sigset_t *signal_mask) {
    if (!attributes || !signal_mask) return EINVAL;
    attributes->sigmask = *signal_mask;
    return 0;
}

int posix_spawnattr_getsigdefault(const posix_spawnattr_t *attributes,
                                  sigset_t *signal_default) {
    if (!attributes || !signal_default) return EINVAL;
    *signal_default = attributes->sigdefault;
    return 0;
}

int posix_spawnattr_setsigdefault(posix_spawnattr_t *attributes,
                                  const sigset_t *signal_default) {
    if (!attributes || !signal_default) return EINVAL;
    attributes->sigdefault = *signal_default;
    return 0;
}

int posix_spawn(pid_t *pid, const char *path,
                const posix_spawn_file_actions_t *file_actions,
                const posix_spawnattr_t *attributes,
                char *const argv[], char *const envp[]) {
    const unsigned short supported_flags = POSIX_SPAWN_RESETIDS |
        POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF |
        POSIX_SPAWN_SETSIGMASK;
    if (!pid || !path || !*path ||
        (file_actions && file_actions->count > 8U) ||
        (attributes && (attributes->flags < 0 ||
         ((unsigned short)attributes->flags &
          (unsigned short)~supported_flags) != 0U)) ||
        (attributes && attributes->pgroup < 0)) return EINVAL;
    char *fallback_argv[2] = {(char *)path, NULL};
    if (!envp && !file_actions &&
        (!attributes || attributes->flags == 0)) {
        /* Use XSCore's native spawn path when no replacement environment is
         * requested.  It avoids the fork/exec address-space transition and
         * preserves the same descriptor/CWD inheritance as the shell. */
        int child = spawn(path, argv ? argv : fallback_argv);
        if (child < 0) return errno ? errno : EAGAIN;
        *pid = child;
        return 0;
    }
    int child = spawn_with_environment(path, argv ? argv : fallback_argv,
                                       envp ? envp : environ, file_actions,
                                       attributes);
    if (child < 0) return errno ? errno : EAGAIN;
    *pid = child;
    return 0;
}

int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *file_actions,
                 const posix_spawnattr_t *attributes,
                 char *const argv[], char *const envp[]) {
    if (!file || !*file) return EINVAL;
    if (strchr(file, '/') != NULL)
        return posix_spawn(pid, file, file_actions, attributes, argv, envp);
    const char *path = getenv("PATH");
    if (!path || !*path) path = "/bin:/usr/bin";
    char candidate[128];
    const char *cursor = path;
    while (1) {
        const char *separator = strchr(cursor, ':');
        size_t directory_length = separator ? (size_t)(separator - cursor) :
                                              strlen(cursor);
        size_t file_length = strlen(file);
        if (directory_length + file_length + 2U < sizeof(candidate)) {
            size_t offset = 0;
            if (directory_length > 0) {
                memcpy(candidate, cursor, directory_length);
                offset = directory_length;
                candidate[offset++] = '/';
            }
            memcpy(candidate + offset, file, file_length + 1U);
            if (access(candidate, X_OK) == 0)
                return posix_spawn(pid, candidate, file_actions, attributes,
                                   argv, envp);
        }
        if (!separator) break;
        cursor = separator + 1U;
    }
    return ENOENT;
}


static long syscall3(uint64_t number, uint64_t argument0, uint64_t argument1,
                     uint64_t argument2) {
#if defined(__x86_64__)
    register uint64_t result __asm__("rax") = number;
    register uint64_t first __asm__("rdi") = argument0;
    register uint64_t second __asm__("rsi") = argument1;
    register uint64_t third __asm__("rdx") = argument2;
    __asm__ volatile("int $0x80"
                     : "+a"(result)
                     : "D"(first), "S"(second), "d"(third)
                     : "cc", "memory");
    return (long)result;
#elif defined(__i386__)
    register uint32_t result __asm__("eax") = (uint32_t)number;
    register uint32_t first __asm__("ebx") = (uint32_t)argument0;
    register uint32_t second __asm__("ecx") = (uint32_t)argument1;
    register uint32_t third __asm__("edx") = (uint32_t)argument2;
    __asm__ volatile("int $0x80"
                     : "+a"(result)
                     : "b"(first), "c"(second), "d"(third)
                     : "cc", "memory");
    return (long)result;
#elif defined(__aarch64__)
    register uint64_t x8 __asm__("x8") = number;
    register uint64_t x0 __asm__("x0") = argument0;
    register uint64_t x1 __asm__("x1") = argument1;
    register uint64_t x2 __asm__("x2") = argument2;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2)
                     : "memory");
    return (long)x0;
#elif defined(__arm__)
    register uint32_t r7 __asm__("r7") = (uint32_t)number;
    register uint32_t r0 __asm__("r0") = (uint32_t)argument0;
    register uint32_t r1 __asm__("r1") = (uint32_t)argument1;
    register uint32_t r2 __asm__("r2") = (uint32_t)argument2;
    __asm__ volatile("svc #0"
                     : "+r"(r0)
                     : "r"(r7), "r"(r1), "r"(r2)
                     : "r3", "r12", "cc", "memory");
    return (long)r0;
#elif defined(__riscv)
#if __riscv_xlen == 64
    register uint64_t a7 __asm__("a7") = number;
    register uint64_t a0 __asm__("a0") = argument0;
    register uint64_t a1 __asm__("a1") = argument1;
    register uint64_t a2 __asm__("a2") = argument2;
    __asm__ volatile("ecall"
                     : "+r"(a0)
                     : "r"(a7), "r"(a1), "r"(a2)
                     : "memory");
    return (long)a0;
#else
    register uint32_t a7 __asm__("a7") = (uint32_t)number;
    register uint32_t a0 __asm__("a0") = (uint32_t)argument0;
    register uint32_t a1 __asm__("a1") = (uint32_t)argument1;
    register uint32_t a2 __asm__("a2") = (uint32_t)argument2;
    __asm__ volatile("ecall"
                     : "+r"(a0)
                     : "r"(a7), "r"(a1), "r"(a2)
                     : "memory");
    return (long)a0;
#endif
#else
#error "Unsupported architecture for syscall3"
#endif
}

/* Raw ABI entry used by the pthread layer.  Unlike the POSIX wrappers below,
 * this preserves the kernel's negative errno convention for easy translation
 * in pthread_create/join/detach. */
long akaos_thread_syscall(uint64_t number, uint64_t argument0,
                          uint64_t argument1, uint64_t argument2) {
    return syscall3(number, argument0, argument1, argument2);
}

static long translate_result(long result) {
    if (result < 0 && result >= -4095) {
        errno = (int)-result;
        return -1;
    }
    return result;
}

ssize_t read(int descriptor, void *buffer, size_t length) {
    ssize_t virtual_result = 0;
    if (__akaos_eventfd_read(descriptor, buffer, length, &virtual_result))
        return virtual_result;
    if (__akaos_timerfd_read(descriptor, buffer, length, &virtual_result))
        return virtual_result;
    return (ssize_t)translate_result(syscall3(
        AKAOS_SYSCALL_READ, (uint64_t)descriptor,
        (uint64_t)(uintptr_t)buffer, (uint64_t)length));
}

ssize_t write(int descriptor, const void *buffer, size_t length) {
    ssize_t virtual_result = 0;
    if (__akaos_eventfd_write(descriptor, buffer, length, &virtual_result))
        return virtual_result;
    if (__akaos_timerfd_write(descriptor, buffer, length, &virtual_result))
        return virtual_result;
    return (ssize_t)translate_result(syscall3(
        AKAOS_SYSCALL_WRITE, (uint64_t)descriptor,
        (uint64_t)(uintptr_t)buffer, (uint64_t)length));
}

static ssize_t vector_io(int descriptor, const struct iovec *vectors,
                         int vector_count, int writing) {
    if (vector_count < 0 || vector_count > IOV_MAX ||
        (vector_count != 0 && !vectors)) {
        errno = EINVAL;
        return -1;
    }
    size_t total = 0U;
    for (int index = 0; index < vector_count; ++index) {
        if (vectors[index].iov_len > (size_t)SSIZE_MAX - total) {
            errno = EINVAL;
            return -1;
        }
        total += vectors[index].iov_len;
    }
    size_t transferred = 0U;
    for (int index = 0; index < vector_count; ++index) {
        if (vectors[index].iov_len == 0U) continue;
        ssize_t amount = writing
            ? write(descriptor, vectors[index].iov_base,
                    vectors[index].iov_len)
            : read(descriptor, vectors[index].iov_base,
                   vectors[index].iov_len);
        if (amount < 0) return transferred ? (ssize_t)transferred : -1;
        transferred += (size_t)amount;
        if ((size_t)amount < vectors[index].iov_len ||
            (size_t)amount == 0U)
            break;
    }
    return (ssize_t)transferred;
}

ssize_t readv(int descriptor, const struct iovec *vectors, int vector_count) {
    return vector_io(descriptor, vectors, vector_count, 0);
}

ssize_t writev(int descriptor, const struct iovec *vectors, int vector_count) {
    return vector_io(descriptor, vectors, vector_count, 1);
}

off_t lseek(int descriptor, off_t offset, int whence) {
    return (off_t)translate_result(syscall3(
        AKAOS_SYSCALL_LSEEK, (uint64_t)(unsigned int)descriptor,
        (uint64_t)(int64_t)offset, (uint64_t)(unsigned int)whence));
}

int close(int descriptor) {
    if (descriptor >= 64 && __akaos_epoll_close(descriptor)) return 0;
    if (descriptor >= 68 && __akaos_eventfd_close(descriptor)) return 0;
    if (descriptor >= 72 && __akaos_timerfd_close(descriptor)) return 0;
    return (int)translate_result(syscall3(AKAOS_SYSCALL_CLOSE,
                                          (uint64_t)descriptor, 0, 0));
}

int dup(int descriptor) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_DUP,
                                          (uint64_t)descriptor, 0, 0));
}

int dup2(int old_descriptor, int new_descriptor) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_DUP2,
                                          (uint64_t)old_descriptor,
                                          (uint64_t)new_descriptor, 0));
}

int fcntl(int descriptor, int command, ...) {
    uint64_t argument = 0U;
    if (command == F_DUPFD || command == F_SETFD || command == F_SETFL ||
        command == F_DUPFD_CLOEXEC || command == F_ADD_SEALS) {
        va_list arguments;
        va_start(arguments, command);
        argument = (uint64_t)(unsigned int)va_arg(arguments, int);
        va_end(arguments);
    }
    int virtual_result = 0;
    if (__akaos_eventfd_fcntl(descriptor, command, argument,
                              &virtual_result)) return virtual_result;
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_FCNTL, (uint64_t)(unsigned int)descriptor,
        (uint64_t)(unsigned int)command, argument));
}

int flock(int descriptor, int operation) {
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_FLOCK, (uint64_t)(unsigned int)descriptor,
        (uint64_t)(unsigned int)operation, 0U));
}

int openat(int directory, const char *path, int flags, ...) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    size_t length = strlen(path);
    if (length == 0 || length > AKAOS_SYSCALL_PATH_MAX) {
        errno = length == 0 ? EINVAL : ENAMETOOLONG;
        return -1;
    }
    mode_t mode = 0644;
    if (flags & O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    struct akaos_openat_args args;
    args.directory = directory;
    args.flags = (uint32_t)flags;
    args.mode = mode;
    args.reserved = 0U;
    args.path = (uint64_t)(uintptr_t)path;
    int descriptor = (int)translate_result(syscall3(
        AKAOS_SYSCALL_OPENAT, (uint64_t)(uintptr_t)&args, 0U, 0U));
    if (descriptor >= 0 && (flags & O_CLOEXEC) &&
        fcntl(descriptor, F_SETFD, FD_CLOEXEC) < 0) {
        int saved_errno = errno;
        (void)close(descriptor);
        errno = saved_errno;
        return -1;
    }
    return descriptor;
}

int open(const char *path, int flags, ...) {
    mode_t mode = 0644;
    if (flags & O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    return openat(AT_FDCWD, path, flags, (int)mode);
}

int creat(const char *path, mode_t mode) {
    return openat(AT_FDCWD, path, O_WRONLY | O_CREAT | O_TRUNC,
                  (int)mode);
}

int akaos_stat(const char *path, struct akaos_stat *status) {
    if (!path || !status) {
        errno = EFAULT;
        return -1;
    }
    size_t length = strlen(path);
    if (length == 0 || length > AKAOS_SYSCALL_PATH_MAX) {
        errno = EINVAL;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_STAT, (uint64_t)(uintptr_t)path, (uint64_t)length,
        (uint64_t)(uintptr_t)status));
}

static void translate_akaos_stat(struct stat *status,
                                 const struct akaos_stat *native_status) {
    memset(status, 0, sizeof(*status));
    status->st_ino = native_status->inode;
    status->st_mode = native_status->mode;
    status->st_nlink = 1U;
    status->st_uid = native_status->uid;
    status->st_gid = native_status->gid;
    status->st_size = (off_t)native_status->size;
    status->st_blksize = 4096;
    status->st_blocks = (blkcnt_t)(native_status->size / 512U +
                                   (native_status->size % 512U != 0U));
}

int stat(const char *path, struct stat *status) {
    if (!path || !status) {
        errno = EFAULT;
        return -1;
    }
    struct akaos_stat native_status;
    if (akaos_stat(path, &native_status) < 0) return -1;
    translate_akaos_stat(status, &native_status);
    return 0;
}

int lstat(const char *path, struct stat *status) {
    /* akaOS does not currently implement symlink vnodes. */
    return stat(path, status);
}

int fstatat(int directory, const char *path, struct stat *status, int flags) {
    if (!path || !status) { errno = EFAULT; return -1; }
    if ((flags & ~AT_SYMLINK_NOFOLLOW) != 0) { errno = EINVAL; return -1; }
    if (directory == AT_FDCWD)
        return (flags & AT_SYMLINK_NOFOLLOW) ? lstat(path, status)
                                              : stat(path, status);

    /* akaOS currently has no symlink vnodes, so following and no-follow
     * status queries are equivalent. Resolve relative paths through openat
     * and obtain metadata from the returned vnode descriptor. */
    int descriptor = openat(directory, path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return -1;
    int result = fstat(descriptor, status);
    int saved_errno = errno;
    (void)close(descriptor);
    errno = saved_errno;
    return result;
}

int fstat(int descriptor, struct stat *status) {
    if (descriptor < 0 || !status) {
        errno = EFAULT;
        return -1;
    }
    struct akaos_stat native_status;
    if (translate_result(syscall3(AKAOS_SYSCALL_FSTAT,
                                  (uint64_t)(unsigned int)descriptor,
                                  (uint64_t)(uintptr_t)&native_status, 0)) < 0)
        return -1;
    translate_akaos_stat(status, &native_status);
    return 0;
}

int statvfs(const char *path, struct statvfs *status) {
    (void)path;
    (void)status;
    /* The current VFS does not expose filesystem capacity counters. */
    errno = ENOSYS;
    return -1;
}

ssize_t getdents(int descriptor, struct akaos_dirent *entries,
                 size_t capacity) {
    return (ssize_t)translate_result(syscall3(
        AKAOS_SYSCALL_GETDENTS, (uint64_t)descriptor,
        (uint64_t)(uintptr_t)entries, (uint64_t)capacity));
}

char *getcwd(char *buffer, size_t size) {
    if (!buffer || size == 0) {
        errno = EINVAL;
        return 0;
    }
    long res = translate_result(syscall3(
        AKAOS_SYSCALL_GETCWD, (uint64_t)(uintptr_t)buffer, (uint64_t)size, 0));
    if (res < 0) return 0;
    return buffer;
}

int chdir(const char *path) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    size_t length = strlen(path);
    if (length == 0 || length > AKAOS_SYSCALL_PATH_MAX) {
        errno = EINVAL;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_CHDIR, (uint64_t)(uintptr_t)path, (uint64_t)length, 0));
}

int fchdir(int descriptor) {
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_FCHDIR, (uint64_t)(unsigned int)descriptor, 0U, 0U));
}

int mkdir(const char *path, mode_t mode) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    size_t length = strlen(path);
    if (length == 0 || length > AKAOS_SYSCALL_PATH_MAX) {
        errno = EINVAL;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_MKDIR, (uint64_t)(uintptr_t)path, (uint64_t)length,
        (uint64_t)mode));
}

/* XSCore exposes pipes as process descriptors, not as filesystem FIFO nodes,
 * so no path can name one.  The interface exists for portable programs that
 * link it; the call reports that the object is not supported. */
int mkfifo(const char *path, mode_t mode) {
    (void)mode;
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    errno = ENOTSUP;
    return -1;
}

int unlink(const char *path) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    size_t length = strlen(path);
    if (length == 0 || length > AKAOS_SYSCALL_PATH_MAX) {
        errno = EINVAL;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_UNLINK, (uint64_t)(uintptr_t)path, (uint64_t)length, 0));
}

int unlinkat(int directory, const char *path, int flags) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    if ((flags & ~AT_REMOVEDIR) != 0) {
        errno = EINVAL;
        return -1;
    }
    size_t length = strlen(path);
    if (length == 0 || length > AKAOS_SYSCALL_PATH_MAX) {
        errno = length == 0 ? EINVAL : ENAMETOOLONG;
        return -1;
    }
    struct akaos_unlinkat_args args;
    args.directory = directory;
    args.flags = (uint32_t)flags;
    args.path = (uint64_t)(uintptr_t)path;
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_UNLINKAT, (uint64_t)(uintptr_t)&args, 0U, 0U));
}

int mkdirat(int directory, const char *path, mode_t mode) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    size_t length = strlen(path);
    if (length == 0U || length > AKAOS_SYSCALL_PATH_MAX) {
        errno = length == 0U ? EINVAL : ENAMETOOLONG;
        return -1;
    }
    struct akaos_mkdirat_args args;
    args.directory = directory;
    args.mode = (uint32_t)mode;
    args.path = (uint64_t)(uintptr_t)path;
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_MKDIRAT, (uint64_t)(uintptr_t)&args, 0U, 0U));
}

int rmdir(const char *path) {
    return unlinkat(AT_FDCWD, path, AT_REMOVEDIR);
}

int rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) {
        errno = EFAULT;
        return -1;
    }
    size_t old_len = strlen(oldpath);
    size_t new_len = strlen(newpath);
    if (old_len == 0 || old_len > AKAOS_SYSCALL_PATH_MAX ||
        new_len == 0 || new_len > AKAOS_SYSCALL_PATH_MAX) {
        errno = EINVAL;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_RENAME, (uint64_t)(uintptr_t)oldpath,
        (uint64_t)(uintptr_t)newpath, 0));
}

int renameat(int old_directory, const char *oldpath, int new_directory,
             const char *newpath) {
    if (!oldpath || !newpath) {
        errno = EFAULT;
        return -1;
    }
    size_t old_length = strlen(oldpath);
    size_t new_length = strlen(newpath);
    if (old_length == 0U || new_length == 0U) {
        errno = EINVAL;
        return -1;
    }
    if (old_length > AKAOS_SYSCALL_PATH_MAX ||
        new_length > AKAOS_SYSCALL_PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    struct akaos_renameat_args args;
    args.old_directory = old_directory;
    args.new_directory = new_directory;
    args.old_path = (uint64_t)(uintptr_t)oldpath;
    args.new_path = (uint64_t)(uintptr_t)newpath;
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_RENAMEAT, (uint64_t)(uintptr_t)&args, 0U, 0U));
}

int spawn(const char *path, char *const argv[]) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    return spawn_with_environment(path, argv, environ, NULL, NULL);
}

static int spawn_with_environment(const char *path, char *const argv[],
                                  char *const envp[],
                                  const posix_spawn_file_actions_t *actions,
                                  const posix_spawnattr_t *attributes) {
    struct akaos_spawnve_args arguments = {
        .path = (uint64_t)(uintptr_t)path,
        .argv = (uint64_t)(uintptr_t)argv,
        .envp = (uint64_t)(uintptr_t)envp,
        .out_pid = 0,
        .actions = (uint64_t)(uintptr_t)(actions ? actions->actions : NULL),
        .action_count = actions ? actions->count : 0U,
        .reserved = 0,
        .spawn_flags = attributes ? (uint32_t)(unsigned short)attributes->flags
                                 : 0U,
        .process_group = attributes ? (uint32_t)attributes->pgroup : 0U,
        .signal_mask = attributes ? attributes->sigmask : 0U,
        .signal_default = attributes ? attributes->sigdefault : 0U,
    };
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_SPAWNVE, (uint64_t)(uintptr_t)&arguments, 0, 0));
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_EXECVE, (uint64_t)(uintptr_t)path,
        (uint64_t)(uintptr_t)argv, (uint64_t)(uintptr_t)envp));
}

int execv(const char *path, char *const argv[]) {
    return execve(path, argv, environ);
}

int execvp(const char *file, char *const argv[]) {
    if (!file || !*file) {
        errno = ENOENT;
        return -1;
    }
    if (strchr(file, '/')) return execve(file, argv, environ);

    const char *search_path = getenv("PATH");
    if (!search_path) search_path = "/bin:/usr/bin";
    int saw_access_denied = 0;
    const char *component = search_path;
    for (;;) {
        const char *separator = strchr(component, ':');
        size_t directory_length = separator
            ? (size_t)(separator - component) : strlen(component);
        size_t file_length = strlen(file);
        size_t needed = file_length + (directory_length ? directory_length + 1U : 0U);
        if (needed < PATH_MAX) {
            char candidate[PATH_MAX];
            size_t offset = 0U;
            if (directory_length) {
                memcpy(candidate, component, directory_length);
                offset = directory_length;
                candidate[offset++] = '/';
            }
            memcpy(candidate + offset, file, file_length);
            candidate[offset + file_length] = '\0';
            (void)execve(candidate, argv, environ);
            if (errno == EACCES) {
                saw_access_denied = 1;
            } else if (errno != ENOENT && errno != ENOTDIR) {
                return -1;
            }
        }
        if (!separator) break;
        component = separator + 1;
    }
    errno = saw_access_denied ? EACCES : ENOENT;
    return -1;
}

/* The search path for a program that is started with an environment of its
 * own comes from that environment. */
static const char *environment_path(char *const envp[]) {
    if (envp) {
        for (size_t index = 0U; envp[index]; ++index) {
            if (strncmp(envp[index], "PATH=", 5U) == 0) return envp[index] + 5U;
        }
    }
    const char *search_path = getenv("PATH");
    return search_path ? search_path : "/bin:/usr/bin";
}

int execvpe(const char *file, char *const argv[], char *const envp[]) {
    if (!file || !*file) {
        errno = ENOENT;
        return -1;
    }
    if (strchr(file, '/')) return execve(file, argv, envp);

    const char *search_path = environment_path(envp);
    int saw_access_denied = 0;
    const char *component = search_path;
    for (;;) {
        const char *separator = strchr(component, ':');
        size_t directory_length = separator
            ? (size_t)(separator - component) : strlen(component);
        size_t file_length = strlen(file);
        size_t needed = file_length + (directory_length ? directory_length + 1U : 0U);
        if (needed < PATH_MAX) {
            char candidate[PATH_MAX];
            size_t offset = 0U;
            if (directory_length) {
                memcpy(candidate, component, directory_length);
                offset = directory_length;
                candidate[offset++] = '/';
            }
            memcpy(candidate + offset, file, file_length);
            candidate[offset + file_length] = '\0';
            (void)execve(candidate, argv, envp);
            if (errno == EACCES) {
                saw_access_denied = 1;
            } else if (errno != ENOENT && errno != ENOTDIR) {
                return -1;
            }
        }
        if (!separator) break;
        component = separator + 1;
    }
    errno = saw_access_denied ? EACCES : ENOENT;
    return -1;
}

/* Collect the arguments of the list form of the exec calls into a vector. */
static int collect_arguments(char ***vector_out, va_list arguments) {
    size_t capacity = 16U;
    size_t count = 0U;
    char **vector = (char **)malloc(capacity * sizeof(char *));
    if (!vector) {
        errno = ENOMEM;
        return -1;
    }
    for (;;) {
        char *value = va_arg(arguments, char *);
        if (count + 1U >= capacity) {
            size_t grown = capacity * 2U;
            char **bigger = (char **)realloc(vector, grown * sizeof(char *));
            if (!bigger) {
                free(vector);
                errno = ENOMEM;
                return -1;
            }
            vector = bigger;
            capacity = grown;
        }
        vector[count++] = value;
        if (!value) break;
    }
    *vector_out = vector;
    return 0;
}

int execl(const char *path, const char *argument, ...) {
    va_list arguments;
    va_start(arguments, argument);
    char **vector = NULL;
    int result = collect_arguments(&vector, arguments);
    va_end(arguments);
    if (result != 0) return -1;
    result = execve(path, vector, environ);
    int saved = errno;
    free(vector);
    errno = saved;
    return result;
}

int execlp(const char *file, const char *argument, ...) {
    va_list arguments;
    va_start(arguments, argument);
    char **vector = NULL;
    int result = collect_arguments(&vector, arguments);
    va_end(arguments);
    if (result != 0) return -1;
    result = execvp(file, vector);
    int saved = errno;
    free(vector);
    errno = saved;
    return result;
}

int execle(const char *path, const char *argument, ...) {
    va_list arguments;
    va_start(arguments, argument);
    char **vector = NULL;
    int result = collect_arguments(&vector, arguments);
    if (result != 0) {
        va_end(arguments);
        return -1;
    }
    /* The environment of the new program follows the arguments. */
    char *const *environment = va_arg(arguments, char *const *);
    result = execve(path, vector, environment);
    int saved = errno;
    free(vector);
    va_end(arguments);
    errno = saved;
    return result;
}

unsigned int if_nametoindex(const char *name) {
    if (!name) {
        errno = EFAULT;
        return 0U;
    }
    if (strcmp(name, "lo") == 0) return 1U;
    if (strcmp(name, "eth0") == 0) return 2U;
    errno = ENODEV;
    return 0U;
}

char *if_indextoname(unsigned int index, char *name) {
    if (!name) {
        errno = EFAULT;
        return NULL;
    }
    const char *source = index == 1U ? "lo" :
                         index == 2U ? "eth0" : NULL;
    if (!source) {
        errno = ENXIO;
        return NULL;
    }
    size_t length = strlen(source);
    memcpy(name, source, length + 1U);
    return name;
}

struct if_nameindex *if_nameindex(void) {
    struct if_nameindex *list = malloc(3U * sizeof(*list));
    if (!list) return NULL;
    list[0].if_index = 1U;
    list[0].if_name = strdup("lo");
    list[1].if_index = 2U;
    list[1].if_name = strdup("eth0");
    list[2].if_index = 0U;
    list[2].if_name = NULL;
    if (!list[0].if_name || !list[1].if_name) {
        if_freenameindex(list);
        errno = ENOMEM;
        return NULL;
    }
    return list;
}

void if_freenameindex(struct if_nameindex *list) {
    if (!list) return;
    for (size_t index = 0U; list[index].if_index != 0U; ++index)
        free(list[index].if_name);
    free(list);
}

void *mmap(void *address, size_t length, int protection, int flags,
           int descriptor, int64_t offset) {
    struct akaos_mmap_args arguments = {
        .address = (uint64_t)(uintptr_t)address,
        .length = (uint64_t)length,
        .protection = (uint64_t)(unsigned int)protection,
        .flags = (uint64_t)(unsigned int)flags,
        .descriptor = descriptor,
        .offset = (uint64_t)offset,
    };
    long result = translate_result(syscall3(
        AKAOS_SYSCALL_MMAP, (uint64_t)(uintptr_t)&arguments, 0, 0));
    return result < 0 ? MAP_FAILED : (void *)(uintptr_t)result;
}

int munmap(void *address, size_t length) {
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_MUNMAP, (uint64_t)(uintptr_t)address,
        (uint64_t)length, 0));
}

int mprotect(void *address, size_t length, int protection) {
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_MPROTECT, (uint64_t)(uintptr_t)address,
        (uint64_t)length, (uint64_t)(unsigned int)protection));
}

int msync(void *address, size_t length, int flags) {
    if (!address || length == 0U ||
        ((uintptr_t)address & (uintptr_t)4095U) != 0U ||
        (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC)) != 0 ||
        (((flags & (MS_ASYNC | MS_SYNC)) != MS_ASYNC) &&
         ((flags & (MS_ASYNC | MS_SYNC)) != MS_SYNC))) {
        errno = EINVAL;
        return -1;
    }
    /* akaOS shared-memory objects are coherent as writes happen; they are
     * anonymous volatile backing, so there is no separate storage writeback. */
    return 0;
}

int shm_open(const char *name, int flags, mode_t mode) {
    if (!name) {
        errno = EFAULT;
        return -1;
    }
    struct akaos_shm_open_args arguments = {
        .name = (uint64_t)(uintptr_t)name,
        .name_length = (uint64_t)strlen(name),
        .flags = (uint64_t)(unsigned int)flags,
        .mode = (uint32_t)mode,
        .reserved = 0,
    };
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_SHM_OPEN, (uint64_t)(uintptr_t)&arguments, 0, 0));
}

int shm_unlink(const char *name) {
    if (!name) {
        errno = EFAULT;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_SHM_UNLINK, (uint64_t)(uintptr_t)name,
        (uint64_t)strlen(name), 0));
}

int memfd_create(const char *name, unsigned int flags) {
    if (!name) {
        errno = EFAULT;
        return -1;
    }
    if (flags & ~(MFD_CLOEXEC | MFD_ALLOW_SEALING)) {
        errno = EINVAL;
        return -1;
    }
    size_t name_length = strlen(name);
    if (name_length > AKAOS_MEMFD_NAME_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    struct akaos_memfd_create_args arguments = {
        .name = (uint64_t)(uintptr_t)name,
        .name_length = (uint32_t)name_length,
        .flags = flags,
    };
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_MEMFD_CREATE, (uint64_t)(uintptr_t)&arguments, 0, 0));
}

int ftruncate(int descriptor, off_t length) {
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_FTRUNCATE, (uint64_t)(unsigned int)descriptor,
        (uint64_t)length, 0));
}

/* Keep the post-syscall copy in a separate call.  ARM32 uses r14 as its
 * link register; retaining a stack pointer in r14 across SVC is unsafe when
 * the syscall blocks and the scheduler resumes another user thread. */
__attribute__((noinline)) static void poll_copy_revents(
    struct pollfd *destination, const struct pollfd *source, nfds_t count) {
    for (nfds_t index = 0; index < count; ++index)
        destination[index].revents = source[index].revents;
}

int poll(struct pollfd *fds, nfds_t count, int timeout) {
    if (count > AKAOS_POLL_MAX_FDS || (count != 0 && !fds)) {
        errno = EINVAL;
        return -1;
    }
    if (count == 0) {
        return (int)translate_result(syscall3(
            AKAOS_SYSCALL_POLL, 0, 0, (uint64_t)(int64_t)timeout));
    }
    struct pollfd translated[AKAOS_POLL_MAX_FDS];
    for (nfds_t index = 0; index < count; ++index) {
        translated[index] = fds[index];
        translated[index].fd = __akaos_eventfd_poll_fd(
            translated[index].fd, translated[index].events);
        translated[index].fd = __akaos_timerfd_poll_fd(
            translated[index].fd, translated[index].events);
    }
    int result = (int)translate_result(syscall3(
        AKAOS_SYSCALL_POLL, (uint64_t)(uintptr_t)translated,
        (uint64_t)count, (uint64_t)(int64_t)timeout));
    if (result >= 0) poll_copy_revents(fds, translated, count);
    return result;
}

int ppoll(struct pollfd *fds, nfds_t count, const struct timespec *timeout,
          const sigset_t *signal_mask) {
    int timeout_ms = -1;
    if (timeout) {
        if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
            timeout->tv_nsec >= 1000000000L) {
            errno = EINVAL;
            return -1;
        }
        if (timeout->tv_sec > INT_MAX / 1000) {
            timeout_ms = INT_MAX;
        } else {
            int64_t milliseconds = (int64_t)timeout->tv_sec * 1000 +
                ((int64_t)timeout->tv_nsec + 999999) / 1000000;
            timeout_ms = milliseconds > INT_MAX ? INT_MAX :
                                                  (int)milliseconds;
        }
    }

    sigset_t previous_mask;
    int restore_mask = 0;
    if (signal_mask) {
        if (sigprocmask(SIG_SETMASK, signal_mask, &previous_mask) != 0)
            return -1;
        restore_mask = 1;
    }
    int result = poll(fds, count, timeout_ms);
    int poll_errno = errno;
    if (restore_mask &&
        sigprocmask(SIG_SETMASK, &previous_mask, NULL) != 0) return -1;
    errno = poll_errno;
    return result;
}

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout) {
    if (nfds < 0 || nfds > FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }
    struct pollfd descriptors[AKAOS_POLL_MAX_FDS];
    int descriptor_numbers[AKAOS_POLL_MAX_FDS];
    nfds_t count = 0;
    for (int descriptor = 0; descriptor < nfds; ++descriptor) {
        short events = 0;
        if (readfds && FD_ISSET(descriptor, readfds)) events |= POLLIN;
        if (writefds && FD_ISSET(descriptor, writefds)) events |= POLLOUT;
        if (exceptfds && FD_ISSET(descriptor, exceptfds)) events |= POLLERR;
        if (!events) continue;
        if (count >= AKAOS_POLL_MAX_FDS) {
            errno = EINVAL;
            return -1;
        }
        descriptors[count].fd = descriptor;
        descriptors[count].events = events;
        descriptors[count].revents = 0;
        descriptor_numbers[count] = descriptor;
        ++count;
    }
    int timeout_ms = -1;
    if (timeout) {
        if (timeout->tv_sec < 0 || timeout->tv_usec < 0 ||
            timeout->tv_usec >= 1000000L) {
            errno = EINVAL;
            return -1;
        }
        if (timeout->tv_sec > 2147483L) timeout_ms = 2147483647;
        else {
            long milliseconds = timeout->tv_sec * 1000L +
                                timeout->tv_usec / 1000L;
            timeout_ms = milliseconds > 2147483647L
                ? 2147483647 : (int)milliseconds;
        }
    }
    if (readfds) FD_ZERO(readfds);
    if (writefds) FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);
    int ready = poll(descriptors, count, timeout_ms);
    if (ready <= 0) return ready;
    int result = 0;
    for (nfds_t index = 0; index < count; ++index) {
        short revents = descriptors[index].revents;
        if (!revents) continue;
        int descriptor = descriptor_numbers[index];
        if (readfds && (revents & (POLLIN | POLLHUP)))
            FD_SET(descriptor, readfds);
        if (writefds && (revents & POLLOUT))
            FD_SET(descriptor, writefds);
        if (exceptfds && (revents & (POLLERR | POLLNVAL)))
            FD_SET(descriptor, exceptfds);
        ++result;
    }
    return result;
}

/* Internal scheduler primitive used by pthreads and resident services. */
int akaos_sched_yield(void) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_YIELD, 0, 0, 0));
}

long akaos_futex(volatile uint32_t *word, uint32_t operation,
                 uint32_t value) {
    return (long)syscall3(AKAOS_SYSCALL_FUTEX,
                          (uint64_t)(uintptr_t)word, operation, value);
}

long akaos_futex_timed(volatile uint32_t *word, uint32_t operation,
                       uint32_t expected, const struct timespec *timeout) {
    if (!word || !timeout || timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
        timeout->tv_nsec >= 1000000000L)
        return -EINVAL;
    struct akaos_futex_timeout arguments = {
        .expected = expected,
        .reserved = 0U,
        .seconds = (uint64_t)timeout->tv_sec,
        .nanoseconds = (uint32_t)timeout->tv_nsec,
        .reserved2 = 0U
    };
    return (long)syscall3(AKAOS_SYSCALL_FUTEX,
                          (uint64_t)(uintptr_t)word,
                          operation | AKAOS_FUTEX_WAIT_TIMEOUT,
                          (uint64_t)(uintptr_t)&arguments);
}

int waitpid(int pid, int *status, int options) {
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_WAITPID, (uint64_t)(unsigned int)pid,
        (uint64_t)(uintptr_t)status, (uint64_t)options));
}

pid_t wait(int *status) {
    return (pid_t)waitpid(-1, status, 0);
}

pid_t wait3(int *status, int options, struct rusage *rusage) {
    (void)rusage;
    return (pid_t)waitpid(-1, status, options);
}

pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage) {
    (void)rusage;
    return (pid_t)waitpid((int)pid, status, options);
}

int pipe(int pipefd[2]) {
    return pipe2(pipefd, 0);
}

int pipe2(int pipefd[2], int flags) {
    if (!pipefd) {
        errno = EFAULT;
        return -1;
    }
    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) {
        errno = EINVAL;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_PIPE, (uint64_t)(uintptr_t)pipefd,
        (uint64_t)(unsigned int)flags, 0));
}

int kill(int pid, int signal) {
    if (pid == 0 || signal < AKAOS_SIGNAL_MIN ||
        signal > AKAOS_SIGNAL_MAX) {
        errno = EINVAL;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_KILL, (uint64_t)(int64_t)pid,
        (uint64_t)(unsigned int)signal, 0));
}

int killpg(int pgrp, int signal) {
    if (pgrp <= 0 || signal < AKAOS_SIGNAL_MIN ||
        signal > AKAOS_SIGNAL_MAX) {
        errno = EINVAL;
        return -1;
    }
    return kill(-pgrp, signal);
}

const char *strsignal(int sig) {
    switch (sig) {
        case 1:  return "Hangup";
        case 2:  return "Interrupt";
        case 3:  return "Quit";
        case 4:  return "Illegal instruction";
        case 5:  return "Trace/breakpoint trap";
        case 6:  return "Aborted";
        case 7:  return "Bus error";
        case 8:  return "Floating point exception";
        case 9:  return "Killed";
        case 10: return "User defined signal 1";
        case 11: return "Segmentation fault";
        case 12: return "User defined signal 2";
        case 13: return "Broken pipe";
        case 14: return "Alarm clock";
        case 15: return "Terminated";
        case 17: return "Child exited";
        case 18: return "Continued";
        case 19: return "Stopped (signal)";
        case 20: return "Stopped";
        case 21: return "Stopped (tty input)";
        case 22: return "Stopped (tty output)";
        default: return "Unknown signal";
    }
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    struct sigaction kact;
    const struct sigaction *pact = act;
    if (act) {
        kact = *act;
        if (!kact.sa_restorer) {
            kact.sa_restorer = __akaos_sigreturn_trampoline;
        }
        pact = &kact;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_SIGACTION, (uint64_t)(unsigned int)signum,
        (uint64_t)(uintptr_t)pact, (uint64_t)(uintptr_t)oldact));
}

int sigemptyset(sigset_t *set) {
    if (!set) { errno = EFAULT; return -1; }
    *set = 0U;
    return 0;
}

int sigfillset(sigset_t *set) {
    if (!set) { errno = EFAULT; return -1; }
    *set = UINT32_MAX;
    return 0;
}

static int valid_signal_set_number(int signal_number) {
    return signal_number > 0 && signal_number < NSIG;
}

int sigaddset(sigset_t *set, int signal_number) {
    if (!set) { errno = EFAULT; return -1; }
    if (!valid_signal_set_number(signal_number)) { errno = EINVAL; return -1; }
    *set |= UINT32_C(1) << (uint32_t)signal_number;
    return 0;
}

int sigdelset(sigset_t *set, int signal_number) {
    if (!set) { errno = EFAULT; return -1; }
    if (!valid_signal_set_number(signal_number)) { errno = EINVAL; return -1; }
    *set &= ~(UINT32_C(1) << (uint32_t)signal_number);
    return 0;
}

int sigismember(const sigset_t *set, int signal_number) {
    if (!set) { errno = EFAULT; return -1; }
    if (!valid_signal_set_number(signal_number)) { errno = EINVAL; return -1; }
    return (*set & (UINT32_C(1) << (uint32_t)signal_number)) != 0U;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *old_set) {
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_SIGPROCMASK, (uint64_t)(unsigned int)how,
        (uint64_t)(uintptr_t)set, (uint64_t)(uintptr_t)old_set));
}

int signalfd(int descriptor, const sigset_t *mask, int flags) {
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_SIGNALFD, (uint64_t)(int64_t)descriptor,
        (uint64_t)(uintptr_t)mask, (uint64_t)(unsigned int)flags));
}

int sigreturn(void) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_SIGRETURN, 0, 0, 0));
}

int raise(int sig) {
    return kill((int)getpid(), sig);
}

sighandler_t signal(int signal_number, sighandler_t handler) {
    struct sigaction act, oldact;
    act.sa_handler = handler;
    act.sa_mask = 0;
    act.sa_flags = 0;
    act.sa_restorer = __akaos_sigreturn_trampoline;
    if (sigaction(signal_number, &act, &oldact) < 0) {
        return SIG_ERR;
    }
    return oldact.sa_handler;
}

int brk(void *addr) {
    uint64_t req = (uint64_t)(uintptr_t)addr;
    uint64_t new_brk = syscall3(AKAOS_SYSCALL_BRK, req, 0, 0);
    if (addr != 0 && new_brk < req) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void *sbrk(intptr_t increment) {
    uint64_t cur_brk = syscall3(AKAOS_SYSCALL_BRK, 0, 0, 0);
    if (cur_brk == 0) {
        errno = ENOMEM;
        return (void *)-1;
    }
    if (increment == 0) {
        return (void *)(uintptr_t)cur_brk;
    }
    uint64_t target_brk = (uint64_t)((intptr_t)cur_brk + increment);
    uint64_t new_brk = syscall3(AKAOS_SYSCALL_BRK, target_brk, 0, 0);
    if (increment > 0 && new_brk < target_brk) {
        errno = ENOMEM;
        return (void *)-1;
    }
    return (void *)(uintptr_t)cur_brk;
}

int sync(void) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_SYNC, 0, 0, 0));
}

int fsync(int descriptor) {
    struct stat status;
    if (fstat(descriptor, &status) != 0) return -1;
    /* XSCore's persistent VFS currently exposes a filesystem-wide journal
     * flush rather than per-vnode writeback. A global sync is stronger than
     * the requested fd barrier and preserves the POSIX success guarantee. */
    return sync();
}

int fdatasync(int descriptor) {
    return fsync(descriptor);
}

int reboot(int cmd) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_REBOOT, (uint64_t)(unsigned int)cmd, 0, 0));
}

int chmod(const char *path, mode_t mode) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_CHMOD, (uint64_t)(uintptr_t)path, (uint64_t)mode, 0));
}

int chown(const char *path, uid_t uid, gid_t gid) {
    if (!path) {
        errno = EFAULT;
        return -1;
    }
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_CHOWN, (uint64_t)(uintptr_t)path, (uint64_t)uid, (uint64_t)gid));
}

int fchown(int fd, uid_t uid, gid_t gid) {
    (void)fd;
    (void)uid;
    (void)gid;
    return 0;
}

uid_t getuid(void) {
    return (uid_t)syscall3(AKAOS_SYSCALL_GETUID, 0, 0, 0);
}

uid_t geteuid(void) {
    return (uid_t)syscall3(AKAOS_SYSCALL_GETEUID, 0, 0, 0);
}

gid_t getgid(void) {
    return (gid_t)syscall3(AKAOS_SYSCALL_GETGID, 0, 0, 0);
}

gid_t getegid(void) {
    return (gid_t)syscall3(AKAOS_SYSCALL_GETEGID, 0, 0, 0);
}

int getgroups(int size, gid_t list[]) {
    if (size < 0) {
        errno = EINVAL;
        return -1;
    }
    if (size == 0) {
        return 1;
    }
    if (!list) {
        errno = EFAULT;
        return -1;
    }
    list[0] = getegid();
    return 1;
}

int setuid(uid_t uid) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_SETUID, uid, 0, 0));
}

int setgid(gid_t gid) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_SETGID, gid, 0, 0));
}

int setpgid(pid_t pid, pid_t pgid) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_SETPGID, (uint64_t)pid, (uint64_t)pgid, 0));
}

pid_t getpgid(pid_t pid) {
    return (pid_t)syscall3(AKAOS_SYSCALL_GETPGID, (uint64_t)pid, 0, 0);
}

pid_t getpgrp(void) {
    return (pid_t)syscall3(AKAOS_SYSCALL_GETPGID, 0, 0, 0);
}

pid_t getsid(pid_t pid) {
    return (pid_t)translate_result(syscall3(AKAOS_SYSCALL_GETSID,
                                             (uint64_t)pid, 0, 0));
}

pid_t setsid(void) {
    return (pid_t)translate_result(syscall3(AKAOS_SYSCALL_SETSID, 0, 0, 0));
}

int tcsetpgrp(int descriptor, pid_t pgrp) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_TCSETPGRP,
                                           (uint64_t)descriptor,
                                           (uint64_t)pgrp, 0));
}

pid_t tcgetpgrp(int descriptor) {
    return (pid_t)translate_result(syscall3(AKAOS_SYSCALL_TCGETPGRP,
                                             (uint64_t)descriptor, 0, 0));
}

int posix_openpt(int flags) {
    return open("/dev/ptmx", flags);
}

static char pty_name_buffer[16];

char *ptsname(int descriptor) {
    int64_t result = translate_result(syscall3(
        AKAOS_SYSCALL_PTSNAME, (uint64_t)descriptor,
        (uint64_t)(uintptr_t)pty_name_buffer, sizeof(pty_name_buffer)));
    return result < 0 ? NULL : pty_name_buffer;
}

int grantpt(int descriptor) {
    return ptsname(descriptor) ? 0 : -1;
}

int unlockpt(int descriptor) {
    return ptsname(descriptor) ? 0 : -1;
}

int ioctl(int descriptor, unsigned long request, void *argument) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_IOCTL,
                                           (uint64_t)descriptor,
                                           (uint64_t)request,
                                           (uint64_t)(uintptr_t)argument));
}

int tcgetattr(int descriptor, struct termios *settings) {
    return ioctl(descriptor, TCGETS, settings);
}

int tcsetattr(int descriptor, int actions, const struct termios *settings) {
    if (actions != TCSANOW && actions != TCSADRAIN && actions != TCSAFLUSH) {
        errno = EINVAL;
        return -1;
    }
    return ioctl(descriptor, TCSETS, (void *)settings);
}

void cfmakeraw(struct termios *settings) {
    if (!settings) return;
    settings->c_iflag &= ~ICRNL;
    settings->c_oflag &= ~(OPOST | ONLCR);
    settings->c_lflag &= ~(ISIG | ICANON | ECHO | ECHOE);
    settings->c_cc[VMIN] = 1;
    settings->c_cc[VTIME] = 0;
}

static char tty_name_buffer[AKAOS_TTY_NAME_MAX];

char *ttyname(int descriptor) {
    return ioctl(descriptor, TIOCGNAME, tty_name_buffer) == 0
               ? tty_name_buffer : NULL;
}

int isatty(int descriptor) {
    struct termios settings;
    return tcgetattr(descriptor, &settings) == 0;
}

pid_t getpid(void) {
    return (pid_t)syscall3(AKAOS_SYSCALL_GETPID, 0, 0, 0);
}

pid_t getppid(void) {
    return (pid_t)syscall3(AKAOS_SYSCALL_GETPPID, 0, 0, 0);
}

pid_t fork(void) {
    struct akaos_pthread_atfork_snapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    __akaos_pthread_atfork_snapshot(&snapshot);
    pthread_t caller_thread = pthread_self();
    __akaos_pthread_atfork_prepare(&snapshot);
    __akaos_pthread_atfork_fork_lock();
    long result = syscall3(AKAOS_SYSCALL_FORK, 0, 0, 0);
    if (result == 0) {
        __akaos_pthread_atfork_child(&snapshot, caller_thread);
    } else {
        __akaos_pthread_atfork_parent(&snapshot);
    }
    return (pid_t)translate_result(result);
}

pid_t vfork(void) {
    /* XSCore has no shared-address-space vfork primitive. The copy-on-write
     * fork path is the safe compatible fallback for child-then-exec callers. */
    return fork();
}

int tty_set_echo(int enabled) {
    struct termios settings;
    if (tcgetattr(STDIN_FILENO, &settings) != 0) return -1;
    int previous = (settings.c_lflag & ECHO) != 0;
    if (enabled) settings.c_lflag |= ECHO;
    else settings.c_lflag &= ~ECHO;
    return tcsetattr(STDIN_FILENO, TCSANOW, &settings) == 0 ? previous : -1;
}

_Noreturn void _exit(int status) {
    __akaos_timerfd_shutdown();
    (void)syscall3(AKAOS_SYSCALL_EXIT, (uint64_t)(unsigned int)status, 0, 0);
    for (;;) {
#if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("pause");
#elif defined(__aarch64__) || defined(__arm__)
        __asm__ volatile("wfe");
#elif defined(__riscv)
        __asm__ volatile("wfi");
#else
        __asm__ volatile("");
#endif
    }
}

int socket(int domain, int type, int protocol) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_SOCKET, (uint64_t)domain, (uint64_t)type, (uint64_t)protocol));
}

static volatile uint32_t socketpair_sequence;

static int socketpair_cleanup(int listener, int client, int peer,
                              const char *path, int path_bound,
                              int saved_errno) {
    if (listener >= 0) (void)close(listener);
    if (client >= 0) (void)close(client);
    if (peer >= 0) (void)close(peer);
    if (path_bound && path) (void)unlink(path);
    errno = saved_errno;
    return -1;
}

static int socketpair_apply_flags(int descriptor, int flags) {
    if ((flags & SOCK_CLOEXEC) != 0 &&
        fcntl(descriptor, F_SETFD, FD_CLOEXEC) != 0) return -1;
    if ((flags & SOCK_NONBLOCK) != 0 &&
        fcntl(descriptor, F_SETFL, O_NONBLOCK) != 0) return -1;
    return 0;
}

int socketpair(int domain, int type, int protocol, int socket_vector[2]) {
    if (!socket_vector) {
        errno = EFAULT;
        return -1;
    }
    if (domain != AF_UNIX) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (protocol != 0) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    int flags = type & (SOCK_CLOEXEC | SOCK_NONBLOCK);
    if ((type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK)) != SOCK_STREAM) {
        errno = EOPNOTSUPP;
        return -1;
    }

    char path[UNIX_PATH_MAX];
    int listener = -1;
    int client = -1;
    int peer = -1;
    int path_bound = 0;
    for (uint32_t attempt = 0; attempt < 16U; ++attempt) {
        uint32_t sequence = __atomic_fetch_add(&socketpair_sequence, 1U,
                                               __ATOMIC_RELAXED);
        int length = snprintf(path, sizeof(path), "/tmp/.akaos-sp-%ld-%u",
                              (long)getpid(), sequence);
        if (length <= 0 || (size_t)length >= sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }

        listener = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listener < 0) return -1;
        struct sockaddr_un address;
        memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        memcpy(address.sun_path, path, (size_t)length + 1U);
        socklen_t address_length = (socklen_t)(offsetof(struct sockaddr_un,
                                                        sun_path) +
                                               (size_t)length + 1U);
        if (bind(listener, (const struct sockaddr *)&address,
                 address_length) == 0) {
            path_bound = 1;
            break;
        }
        int bind_errno = errno;
        (void)close(listener);
        listener = -1;
        if (bind_errno != EADDRINUSE) {
            errno = bind_errno;
            return -1;
        }
    }
    if (!path_bound) {
        errno = EADDRINUSE;
        return -1;
    }
    if (listen(listener, 1) != 0)
        return socketpair_cleanup(listener, -1, -1, path, path_bound, errno);

    client = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client < 0)
        return socketpair_cleanup(listener, -1, -1, path, path_bound, errno);
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    size_t path_length = strlen(path);
    memcpy(address.sun_path, path, path_length + 1U);
    socklen_t address_length = (socklen_t)(offsetof(struct sockaddr_un,
                                                    sun_path) +
                                           path_length + 1U);
    if (connect(client, (const struct sockaddr *)&address,
                address_length) != 0)
        return socketpair_cleanup(listener, client, -1, path, path_bound,
                                  errno);
    peer = accept(listener, NULL, NULL);
    if (peer < 0)
        return socketpair_cleanup(listener, client, -1, path, path_bound,
                                  errno);

    int saved_errno = 0;
    if (socketpair_apply_flags(client, flags) != 0 ||
        socketpair_apply_flags(peer, flags) != 0) {
        saved_errno = errno;
        return socketpair_cleanup(listener, client, peer, path, path_bound,
                                  saved_errno);
    }
    if (close(listener) != 0) {
        saved_errno = errno;
        return socketpair_cleanup(-1, client, peer, path, path_bound,
                                  saved_errno);
    }
    listener = -1;
    if (unlink(path) != 0 && errno != ENOENT) {
        saved_errno = errno;
        return socketpair_cleanup(listener, client, peer, path, path_bound,
                                  saved_errno);
    }
    path_bound = 0;
    socket_vector[0] = client;
    socket_vector[1] = peer;
    return 0;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_CONNECT, (uint64_t)sockfd, (uint64_t)(uintptr_t)addr, (uint64_t)addrlen));
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_BIND, (uint64_t)sockfd, (uint64_t)(uintptr_t)addr, (uint64_t)addrlen));
}

int listen(int sockfd, int backlog) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_LISTEN, (uint64_t)sockfd, (uint64_t)backlog, 0));
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_ACCEPT, (uint64_t)sockfd, (uint64_t)(uintptr_t)addr, (uint64_t)(uintptr_t)addrlen));
}

int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_GETSOCKNAME, (uint64_t)(uint32_t)sockfd,
        (uint64_t)(uintptr_t)addr, (uint64_t)(uintptr_t)addrlen));
}

int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_GETPEERNAME, (uint64_t)(uint32_t)sockfd,
        (uint64_t)(uintptr_t)addr, (uint64_t)(uintptr_t)addrlen));
}

int getsockopt(int sockfd, int level, int option, void *optval,
               socklen_t *optlen) {
    struct akaos_getsockopt_args arguments;
    arguments.sockfd = (uint64_t)(uint32_t)sockfd;
    arguments.level = (uint32_t)level;
    arguments.option = (uint32_t)option;
    arguments.optval = (uint64_t)(uintptr_t)optval;
    arguments.optlen = (uint64_t)(uintptr_t)optlen;
    return (int)translate_result(syscall3(
        AKAOS_SYSCALL_GETSOCKOPT,
        (uint64_t)(uintptr_t)&arguments, 0, 0));
}

int setsockopt(int sockfd, int level, int option, const void *optval,
               socklen_t optlen) {
    struct akaos_setsockopt_args arguments = {
        .sockfd = (uint64_t)(uint32_t)sockfd,
        .level = (uint32_t)level,
        .option = (uint32_t)option,
        .optval = (uint64_t)(uintptr_t)optval,
        .optlen = optlen,
    };
    return (int)translate_result(syscall3(AKAOS_SYSCALL_SETSOCKOPT,
        (uint64_t)(uintptr_t)&arguments, 0U, 0U));
}

int shutdown(int sockfd, int how) {
    return (int)translate_result(syscall3(AKAOS_SYSCALL_SHUTDOWN,
        (uint64_t)(uint32_t)sockfd, (uint64_t)(unsigned int)how, 0U));
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags) {
    (void)flags;
    return write(sockfd, buf, len);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen) {
    (void)flags; (void)addrlen;
    if (!dest_addr) return write(sockfd, buf, len);
    const struct sockaddr_in *sin = (const struct sockaddr_in *)dest_addr;
    struct akaos_sock_io_args io_args;
    io_args.len = (uint32_t)len;
    io_args.ip = sin->sin_addr.s_addr;
    io_args.port = ntohs(sin->sin_port);
    io_args.reserved = 0;
    return (ssize_t)translate_result(syscall3(AKAOS_SYSCALL_SENDTO, (uint64_t)sockfd, (uint64_t)(uintptr_t)buf, (uint64_t)(uintptr_t)&io_args));
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags) {
    (void)flags;
    return read(sockfd, buf, len);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                 struct sockaddr *src_addr, socklen_t *addrlen) {
    (void)flags;
    struct akaos_sock_io_args io_args;
    io_args.len = (uint32_t)len;
    io_args.ip = 0;
    io_args.port = 0;
    io_args.reserved = 0;
    int64_t result = translate_result(syscall3(AKAOS_SYSCALL_RECVFROM, (uint64_t)sockfd, (uint64_t)(uintptr_t)buf, (uint64_t)(uintptr_t)&io_args));
    if (result >= 0 && src_addr) {
        struct sockaddr_in *sin = (struct sockaddr_in *)src_addr;
        sin->sin_family = AF_INET;
        sin->sin_addr.s_addr = io_args.ip;
        sin->sin_port = htons(io_args.port);
        if (addrlen) *addrlen = sizeof(struct sockaddr_in);
    }
    return (ssize_t)result;
}

_Static_assert(sizeof(struct msghdr) ==
               sizeof(struct akaos_socket_msghdr),
               "libc/kernel msghdr ABI mismatch");
_Static_assert(offsetof(struct msghdr, msg_iov) ==
               offsetof(struct akaos_socket_msghdr, iov) &&
               offsetof(struct msghdr, msg_controllen) ==
               offsetof(struct akaos_socket_msghdr, control_length) &&
               offsetof(struct msghdr, msg_flags) ==
               offsetof(struct akaos_socket_msghdr, flags),
               "libc/kernel msghdr field offset mismatch");

ssize_t sendmsg(int sockfd, const struct msghdr *message, int flags) {
    return (ssize_t)translate_result(syscall3(
        AKAOS_SYSCALL_SENDMSG, (uint64_t)(uint32_t)sockfd,
        (uint64_t)(uintptr_t)message, (uint64_t)(uint32_t)flags));
}

ssize_t recvmsg(int sockfd, struct msghdr *message, int flags) {
    return (ssize_t)translate_result(syscall3(
        AKAOS_SYSCALL_RECVMSG, (uint64_t)(uint32_t)sockfd,
        (uint64_t)(uintptr_t)message, (uint64_t)(uint32_t)flags));
}

uint16_t htons(uint16_t hostshort) {
    return (uint16_t)((hostshort >> 8) | (hostshort << 8));
}

uint16_t ntohs(uint16_t netshort) {
    return (uint16_t)((netshort >> 8) | (netshort << 8));
}

uint32_t htonl(uint32_t hostlong) {
    return ((hostlong >> 24) & 0xff) |
           ((hostlong >> 8) & 0xff00) |
           ((hostlong << 8) & 0xff0000) |
           ((hostlong << 24) & 0xff000000);
}

uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);
}

uint32_t inet_addr(const char *cp) {
    if (!cp) return 0;
    uint32_t parts[4] = {0, 0, 0, 0};
    int idx = 0;
    while (*cp) {
        if (*cp >= '0' && *cp <= '9') {
            parts[idx] = parts[idx] * 10 + (*cp - '0');
        } else if (*cp == '.') {
            idx++;
            if (idx > 3) return 0;
        } else {
            return 0;
        }
        cp++;
    }
    if (idx != 3) return 0;
    return parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
}

int inet_aton(const char *cp, struct in_addr *inp) {
    uint32_t addr = inet_addr(cp);
    if (addr == 0 && cp[0] != '0') return 0;
    if (inp) inp->s_addr = addr;
    return 1;
}

static char inet_ntoa_buf[32];
char *inet_ntoa(struct in_addr in) {
    uint32_t a = in.s_addr;
    snprintf(inet_ntoa_buf, sizeof(inet_ntoa_buf), "%u.%u.%u.%u",
             a & 0xff, (a >> 8) & 0xff, (a >> 16) & 0xff, (a >> 24) & 0xff);
    return inet_ntoa_buf;
}

int inet_pton(int family, const char *source, void *destination) {
    if (!source || !destination) { errno = EFAULT; return -1; }
    if (family != AF_INET) { errno = EAFNOSUPPORT; return -1; }
    uint8_t octets[4];
    const char *cursor = source;
    for (size_t part = 0U; part < 4U; ++part) {
        if (*cursor < '0' || *cursor > '9') return 0;
        unsigned value = 0U;
        size_t digits = 0U;
        while (*cursor >= '0' && *cursor <= '9') {
            if (digits == 3U) return 0;
            value = value * 10U + (unsigned)(*cursor++ - '0');
            if (value > 255U) return 0;
            ++digits;
        }
        if (digits > 1U && cursor[-(ptrdiff_t)digits] == '0') return 0;
        octets[part] = (uint8_t)value;
        if (part < 3U) {
            if (*cursor++ != '.') return 0;
        } else if (*cursor != '\0') {
            return 0;
        }
    }
    memcpy(destination, octets, sizeof(octets));
    return 1;
}

const char *inet_ntop(int family, const void *source, char *destination,
                      socklen_t destination_size) {
    if (!source || !destination) { errno = EFAULT; return NULL; }
    if (family != AF_INET) { errno = EAFNOSUPPORT; return NULL; }
    const uint8_t *octets = source;
    int length = snprintf(destination, destination_size, "%u.%u.%u.%u",
                          (unsigned)octets[0], (unsigned)octets[1],
                          (unsigned)octets[2], (unsigned)octets[3]);
    if (length < 0 || (socklen_t)length >= destination_size) {
        errno = ENOSPC;
        return NULL;
    }
    return destination;
}

static volatile uint32_t resolver_query_sequence = UINT32_C(0x1233);

static uint32_t resolver_nameserver_ipv4(void) {
    uint32_t dns_ip = (10U) | (0U << 8U) | (2U << 16U) | (3U << 24U);
    int descriptor = open("/etc/resolv.conf", O_RDONLY);
    if (descriptor < 0) return dns_ip;

    char contents[512];
    ssize_t length = read(descriptor, contents, sizeof(contents) - 1U);
    close(descriptor);
    if (length <= 0) return dns_ip;
    contents[(size_t)length] = '\0';

    const char *line = contents;
    while (*line) {
        while (*line == ' ' || *line == '\t') ++line;
        if (strncmp(line, "nameserver", 10U) == 0 &&
            (line[10] == ' ' || line[10] == '\t')) {
            const char *address = line + 10U;
            while (*address == ' ' || *address == '\t') ++address;
            char text[32];
            size_t used = 0U;
            while (address[used] && address[used] != ' ' &&
                   address[used] != '\t' && address[used] != '\r' &&
                   address[used] != '\n' && address[used] != '#' &&
                   used + 1U < sizeof(text)) {
                text[used] = address[used];
                ++used;
            }
            text[used] = '\0';
            uint32_t configured = inet_addr(text);
            if (configured != 0U) dns_ip = configured;
            break;
        }
        const char *newline = strchr(line, '\n');
        if (!newline) break;
        line = newline + 1U;
    }
    return dns_ip;
}

int res_query(const char *domain_name, int query_class, int query_type,
              unsigned char *answer, int answer_length) {
    if (!domain_name || !answer || answer_length < 12 || query_class <= 0 ||
        query_class > UINT16_MAX || query_type <= 0 ||
        query_type > UINT16_MAX) {
        h_errno = NO_RECOVERY;
        errno = EINVAL;
        return -1;
    }

    size_t domain_length = strlen(domain_name);
    if (domain_length > 253U) {
        h_errno = NO_RECOVERY;
        errno = ENAMETOOLONG;
        return -1;
    }

    unsigned char query[512] = {0};
    uint16_t query_id = (uint16_t)__atomic_add_fetch(
        &resolver_query_sequence, 1U, __ATOMIC_RELAXED);
    if (query_id == 0U) query_id = 1U;
    query[0] = (unsigned char)(query_id >> 8U);
    query[1] = (unsigned char)query_id;
    query[2] = 0x01U; /* Recursion desired. */
    query[5] = 0x01U; /* QDCOUNT. */

    size_t offset = 12U;
    const char *label = domain_name;
    while (*label) {
        const char *dot = strchr(label, '.');
        size_t label_length = dot ? (size_t)(dot - label) : strlen(label);
        if (label_length == 0U && dot && dot[1] == '\0') break;
        if (label_length == 0U || label_length > 63U ||
            offset + 1U + label_length + 5U > sizeof(query)) {
            h_errno = NO_RECOVERY;
            errno = label_length > 63U ? ENAMETOOLONG : EINVAL;
            return -1;
        }
        query[offset++] = (unsigned char)label_length;
        memcpy(query + offset, label, label_length);
        offset += label_length;
        if (!dot) break;
        label = dot + 1U;
    }
    query[offset++] = 0U;
    query[offset++] = (unsigned char)((uint16_t)query_type >> 8U);
    query[offset++] = (unsigned char)query_type;
    query[offset++] = (unsigned char)((uint16_t)query_class >> 8U);
    query[offset++] = (unsigned char)query_class;

    uint32_t dns_ip = resolver_nameserver_ipv4();
    int descriptor = socket(AF_INET, SOCK_DGRAM, 0);
    if (descriptor < 0) {
        h_errno = TRY_AGAIN;
        return -1;
    }
    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(53U);
    server.sin_addr.s_addr = dns_ip;
    ssize_t sent = sendto(descriptor, query, offset, 0,
                          (const struct sockaddr *)&server, sizeof(server));
    if (sent != (ssize_t)offset) {
        int saved_errno = errno ? errno : EIO;
        close(descriptor);
        h_errno = TRY_AGAIN;
        errno = saved_errno;
        return -1;
    }

    struct pollfd ready = {descriptor, POLLIN, 0};
    int poll_result = poll(&ready, 1U, 2000);
    if (poll_result <= 0 || !(ready.revents & POLLIN)) {
        int saved_errno = poll_result < 0 ? errno : ETIMEDOUT;
        close(descriptor);
        h_errno = TRY_AGAIN;
        errno = saved_errno;
        return -1;
    }

    struct sockaddr_in responder;
    socklen_t responder_length = sizeof(responder);
    ssize_t received = recvfrom(descriptor, answer, (size_t)answer_length, 0,
                                (struct sockaddr *)&responder,
                                &responder_length);
    int saved_errno = errno;
    close(descriptor);
    if (received < 12) {
        h_errno = TRY_AGAIN;
        errno = received < 0 ? saved_errno : EIO;
        return -1;
    }
    if (answer[0] != query[0] || answer[1] != query[1] ||
        !(answer[2] & 0x80U)) {
        h_errno = NO_RECOVERY;
        errno = EIO;
        return -1;
    }

    unsigned response_code = answer[3] & 0x0fU;
    if (response_code == 3U) {
        h_errno = HOST_NOT_FOUND;
        errno = ENOENT;
        return -1;
    }
    if (response_code != 0U) {
        h_errno = response_code == 2U ? TRY_AGAIN : NO_RECOVERY;
        errno = response_code == 2U ? EAGAIN : EIO;
        return -1;
    }
    uint16_t answer_count =
        (uint16_t)(((uint16_t)answer[6] << 8U) | answer[7]);
    h_errno = answer_count == 0U ? NO_DATA : 0;
    errno = 0;
    return (int)received;
}

static struct hostent s_hostent;
static char s_host_name[128];
static uint32_t s_host_addr;
static char *s_addr_list[2];

struct akaos_service_record {
    const char *name;
    uint16_t port;
    const char *protocol;
    const char *aliases[4];
};

static const struct akaos_service_record akaos_services[] = {
    {"http", 80U, "tcp", {"www", NULL, NULL, NULL}},
    {"https", 443U, "tcp", {"www-https", NULL, NULL, NULL}},
    {"ssh", 22U, "tcp", {NULL, NULL, NULL, NULL}},
    {"ftp", 21U, "tcp", {NULL, NULL, NULL, NULL}},
    {"ftp-data", 20U, "tcp", {NULL, NULL, NULL, NULL}},
    {"smtp", 25U, "tcp", {"mail", NULL, NULL, NULL}},
    {"domain", 53U, "udp", {"dns", NULL, NULL, NULL}},
    {"domain", 53U, "tcp", {"dns", NULL, NULL, NULL}},
    {"ntp", 123U, "udp", {NULL, NULL, NULL, NULL}},
    {"imap", 143U, "tcp", {NULL, NULL, NULL, NULL}},
    {"imaps", 993U, "tcp", {NULL, NULL, NULL, NULL}},
    {"pop3", 110U, "tcp", {NULL, NULL, NULL, NULL}},
    {"pop3s", 995U, "tcp", {NULL, NULL, NULL, NULL}},
};

static struct servent akaos_service_result;
static char *akaos_service_aliases[4];
static size_t akaos_service_iteration;

static struct servent *service_from_record(
    const struct akaos_service_record *record) {
    if (!record) return NULL;
    akaos_service_result.s_name = (char *)record->name;
    akaos_service_result.s_proto = (char *)record->protocol;
    akaos_service_result.s_port = (int)htons(record->port);
    for (size_t index = 0U; index < 4U; ++index)
        akaos_service_aliases[index] = (char *)record->aliases[index];
    akaos_service_result.s_aliases = akaos_service_aliases;
    return &akaos_service_result;
}

static int service_name_matches(const struct akaos_service_record *record,
                                const char *name) {
    if (strcmp(record->name, name) == 0) return 1;
    for (size_t index = 0U; index < 4U && record->aliases[index]; ++index)
        if (strcmp(record->aliases[index], name) == 0) return 1;
    return 0;
}

struct servent *getservbyname(const char *name, const char *protocol) {
    if (!name) { errno = EFAULT; return NULL; }
    for (size_t index = 0U;
         index < sizeof(akaos_services) / sizeof(akaos_services[0]); ++index) {
        const struct akaos_service_record *record = &akaos_services[index];
        if (service_name_matches(record, name) &&
            (!protocol || strcmp(protocol, record->protocol) == 0))
            return service_from_record(record);
    }
    errno = ENOENT;
    return NULL;
}

struct servent *getservbyport(int port, const char *protocol) {
    uint16_t host_port = ntohs((uint16_t)port);
    for (size_t index = 0U;
         index < sizeof(akaos_services) / sizeof(akaos_services[0]); ++index) {
        const struct akaos_service_record *record = &akaos_services[index];
        if (record->port == host_port &&
            (!protocol || strcmp(protocol, record->protocol) == 0))
            return service_from_record(record);
    }
    errno = ENOENT;
    return NULL;
}

void setservent(int stay_open) {
    (void)stay_open;
    akaos_service_iteration = 0U;
}

void endservent(void) {
    akaos_service_iteration = 0U;
}

struct servent *getservent(void) {
    if (akaos_service_iteration >=
        sizeof(akaos_services) / sizeof(akaos_services[0]))
        return NULL;
    return service_from_record(&akaos_services[akaos_service_iteration++]);
}

int getservbyname_r(const char *name, const char *protocol,
                    struct servent *entry, char *buffer,
                    size_t buffer_size, struct servent **result) {
    if (!name || !entry || !buffer || !result) return EINVAL;
    *result = NULL;
    struct servent *source = getservbyname(name, protocol);
    if (!source) return ENOENT;
    size_t name_length = strlen(source->s_name) + 1U;
    size_t protocol_length = strlen(source->s_proto) + 1U;
    if (name_length > buffer_size ||
        protocol_length > buffer_size - name_length)
        return ERANGE;
    memcpy(buffer, source->s_name, name_length);
    memcpy(buffer + name_length, source->s_proto, protocol_length);
    entry->s_name = buffer;
    entry->s_proto = buffer + name_length;
    entry->s_port = source->s_port;
    entry->s_aliases = NULL;
    *result = entry;
    return 0;
}

int dn_skipname(const u_char *compressed_name, const u_char *message_end) {
    if (!compressed_name || !message_end || compressed_name >= message_end)
        return -1;
    const u_char *cursor = compressed_name;
    size_t consumed = 0U;
    for (;;) {
        if (cursor >= message_end) return -1;
        unsigned int length = *cursor++;
        ++consumed;
        if ((length & 0xc0U) == 0xc0U) {
            if (cursor >= message_end) return -1;
            return consumed + 1U > (size_t)INT32_MAX ? -1
                                                       : (int)(consumed + 1U);
        }
        if ((length & 0xc0U) != 0U || length > 63U) return -1;
        if (length == 0U) return consumed > (size_t)INT32_MAX
            ? -1 : (int)consumed;
        if ((size_t)(message_end - cursor) < length) return -1;
        cursor += length;
        consumed += length;
    }
}

int dn_expand(const u_char *message, const u_char *message_end,
              const u_char *compressed_name, char *expanded_name,
              int expanded_capacity) {
    if (!message || !message_end || !compressed_name || !expanded_name ||
        expanded_capacity <= 0 || message_end < message ||
        compressed_name < message || compressed_name >= message_end)
        return -1;
    const u_char *cursor = compressed_name;
    const u_char *resume = NULL;
    size_t consumed = 0U;
    size_t written = 0U;
    unsigned int jumps = 0U;
    for (;;) {
        if (cursor >= message_end) return -1;
        unsigned int length = *cursor++;
        if ((length & 0xc0U) == 0xc0U) {
            if (cursor >= message_end || ++jumps > 128U) return -1;
            size_t offset = ((size_t)(length & 0x3fU) << 8) | *cursor++;
            if (offset >= (size_t)(message_end - message)) return -1;
            if (!resume) {
                consumed = (size_t)(cursor - compressed_name);
                resume = cursor;
            }
            cursor = message + offset;
            continue;
        }
        if ((length & 0xc0U) != 0U || length > 63U) return -1;
        if (length == 0U) {
            if (!resume) consumed = (size_t)(cursor - compressed_name);
            if (written == 0U) {
                if (expanded_capacity < 2) return -1;
                expanded_name[written++] = '.';
            }
            expanded_name[written] = '\0';
            return consumed > (size_t)INT32_MAX ? -1 : (int)consumed;
        }
        if ((size_t)(message_end - cursor) < length) return -1;
        if (written != 0U) {
            if (written + 1U >= (size_t)expanded_capacity) return -1;
            expanded_name[written++] = '.';
        }
        if (written + length >= (size_t)expanded_capacity) return -1;
        for (unsigned int index = 0U; index < length; ++index)
            expanded_name[written++] = (char)cursor[index];
        cursor += length;
    }
}

static int resolver_reverse_ipv4(const struct in_addr *address, char *name,
                                 size_t name_capacity) {
    if (!address || !name || name_capacity == 0U) return EAI_NONAME;
    const uint8_t *bytes = (const uint8_t *)(const void *)&address->s_addr;
    char query[64];
    int query_length = snprintf(query, sizeof(query),
                                "%u.%u.%u.%u.in-addr.arpa",
                                (unsigned)bytes[3], (unsigned)bytes[2],
                                (unsigned)bytes[1], (unsigned)bytes[0]);
    if (query_length < 0 || (size_t)query_length >= sizeof(query))
        return EAI_FAIL;
    u_char answer[2048];
    int answer_length = res_query(query, C_IN, T_PTR, answer,
                                  (int)sizeof(answer));
    if (answer_length < (int)sizeof(HEADER)) return EAI_NONAME;
    const u_char *end = answer + answer_length;
    const HEADER *header = (const HEADER *)(const void *)answer;
    const u_char *cursor = answer + sizeof(HEADER);
    uint16_t question_count = ntohs(header->qdcount);
    uint16_t answer_count = ntohs(header->ancount);
    for (uint16_t index = 0U; index < question_count; ++index) {
        int skipped = dn_skipname(cursor, end);
        if (skipped < 0 || (size_t)(end - cursor) < (size_t)skipped + 4U)
            return EAI_FAIL;
        cursor += (size_t)skipped + 4U;
    }
    for (uint16_t index = 0U; index < answer_count; ++index) {
        int skipped = dn_skipname(cursor, end);
        if (skipped < 0 || (size_t)(end - cursor) < (size_t)skipped + 10U)
            return EAI_FAIL;
        cursor += skipped;
        uint16_t type = 0U, query_class = 0U, data_length = 0U;
        uint32_t ttl = 0U;
        GETSHORT(type, cursor);
        GETSHORT(query_class, cursor);
        GETLONG(ttl, cursor);
        GETSHORT(data_length, cursor);
        (void)ttl;
        if ((size_t)(end - cursor) < data_length) return EAI_FAIL;
        if (type == T_PTR && query_class == C_IN) {
            int expanded = dn_expand(answer, end, cursor, name,
                                     (int)name_capacity);
            if (expanded >= 0) return 0;
        }
        cursor += data_length;
    }
    return EAI_NONAME;
}

static int copy_nameinfo_text(char *destination, socklen_t capacity,
                              const char *source) {
    if (!destination) return 0;
    size_t length = strlen(source);
    if (capacity == 0U || length >= (size_t)capacity) return EAI_OVERFLOW;
    memcpy(destination, source, length + 1U);
    return 0;
}

int getnameinfo(const struct sockaddr *address, socklen_t address_length,
                char *host, socklen_t host_length, char *service,
                socklen_t service_length, int flags) {
    const int supported_flags = NI_NOFQDN | NI_NUMERICHOST | NI_NAMEREQD |
                                NI_NUMERICSERV | NI_DGRAM | NI_WITHSCOPEID;
    if (!address || (host && host_length == 0U) ||
        (service && service_length == 0U) || (flags & ~supported_flags))
        return EAI_BADFLAGS;
    if (address_length < sizeof(sa_family_t)) return EAI_FAMILY;

    uint16_t family = address->sa_family;
    uint16_t port = 0U;
    struct in_addr ipv4;
    if (family == AF_INET) {
        if (address_length < sizeof(struct sockaddr_in)) return EAI_FAMILY;
        const struct sockaddr_in *ipv4_address =
            (const struct sockaddr_in *)(const void *)address;
        ipv4 = ipv4_address->sin_addr;
        port = ipv4_address->sin_port;
    } else {
        return EAI_FAMILY;
    }

    if (host) {
        char host_buffer[INET_ADDRSTRLEN];
        int reverse_result = EAI_NONAME;
        if (!(flags & NI_NUMERICHOST))
            reverse_result = resolver_reverse_ipv4(&ipv4, host_buffer,
                                                   sizeof(host_buffer));
        if (reverse_result == 0) {
            if ((flags & NI_NOFQDN) != 0) {
                char *dot = strchr(host_buffer, '.');
                if (dot) *dot = '\0';
            }
        } else {
            if (flags & NI_NAMEREQD) return reverse_result;
            if (!inet_ntop(AF_INET, &ipv4, host_buffer,
                           (socklen_t)sizeof(host_buffer)))
                return EAI_FAIL;
        }
        int error = copy_nameinfo_text(host, host_length, host_buffer);
        if (error != 0) return error;
    }

    if (service) {
        char service_buffer[NI_MAXSERV];
        struct servent *entry = NULL;
        if (!(flags & NI_NUMERICSERV))
            entry = getservbyport(port, (flags & NI_DGRAM) ? "udp" : "tcp");
        if (entry) {
            memcpy(service_buffer, entry->s_name,
                   strlen(entry->s_name) + 1U);
        } else {
            int written = snprintf(service_buffer, sizeof(service_buffer),
                                   "%u", (unsigned)ntohs(port));
            if (written < 0 || (size_t)written >= sizeof(service_buffer))
                return EAI_FAIL;
        }
        int error = copy_nameinfo_text(service, service_length,
                                       service_buffer);
        if (error != 0) return error;
    }
    return 0;
}

struct hostent *gethostbyname(const char *name) {
    if (!name || !*name) return NULL;

    uint32_t parsed_ip = inet_addr(name);
    if (parsed_ip != 0 || strcmp(name, "0.0.0.0") == 0) {
        strncpy(s_host_name, name, sizeof(s_host_name) - 1);
        s_host_name[sizeof(s_host_name) - 1] = '\0';
        s_host_addr = parsed_ip;
        s_addr_list[0] = (char *)&s_host_addr;
        s_addr_list[1] = NULL;
        s_hostent.h_name = s_host_name;
        s_hostent.h_aliases = NULL;
        s_hostent.h_addrtype = AF_INET;
        s_hostent.h_length = sizeof(uint32_t);
        s_hostent.h_addr_list = s_addr_list;
        return &s_hostent;
    }

    if (strcmp(name, "localhost") == 0) {
        strncpy(s_host_name, name, sizeof(s_host_name) - 1);
        s_host_name[sizeof(s_host_name) - 1] = '\0';
        s_host_addr = (127) | (0 << 8) | (0 << 16) | (1 << 24);
        s_addr_list[0] = (char *)&s_host_addr;
        s_addr_list[1] = NULL;
        s_hostent.h_name = s_host_name;
        s_hostent.h_aliases = NULL;
        s_hostent.h_addrtype = AF_INET;
        s_hostent.h_length = sizeof(uint32_t);
        s_hostent.h_addr_list = s_addr_list;
        return &s_hostent;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return NULL;

    uint32_t dns_ip = (10) | (0 << 8) | (2 << 16) | (3 << 24); /* default 10.0.2.3 */
    int rfd = open("/etc/resolv.conf", O_RDONLY);
    if (rfd >= 0) {
        char rbuf[256];
        ssize_t rn = read(rfd, rbuf, sizeof(rbuf) - 1);
        if (rn > 0) {
            rbuf[rn] = '\0';
            char *line = rbuf;
            while (*line) {
                if (strncmp(line, "nameserver", 10) == 0) {
                    char *p = line + 10;
                    while (*p == ' ' || *p == '\t') p++;
                    char ip_str[32];
                    int i = 0;
                    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < 31) {
                        ip_str[i++] = *p++;
                    }
                    ip_str[i] = '\0';
                    uint32_t custom = inet_addr(ip_str);
                    if (custom != 0) dns_ip = custom;
                    break;
                }
                char *nl = strchr(line, '\n');
                if (!nl) break;
                line = nl + 1;
            }
        }
        close(rfd);
    }

    uint8_t packet[512];
    memset(packet, 0, sizeof(packet));

    packet[0] = 0x12; packet[1] = 0x34; /* ID */
    packet[2] = 0x01; packet[3] = 0x00; /* RD=1 */
    packet[4] = 0x00; packet[5] = 0x01; /* QDCOUNT=1 */

    int qpos = 12;
    const char *src = name;
    while (*src) {
        const char *dot = strchr(src, '.');
        size_t len = dot ? (size_t)(dot - src) : strlen(src);
        if (len > 63 || qpos + 1 + len >= sizeof(packet) - 10) {
            close(sock);
            return NULL;
        }
        packet[qpos++] = (uint8_t)len;
        memcpy(packet + qpos, src, len);
        qpos += (int)len;
        if (!dot) break;
        src = dot + 1;
    }
    packet[qpos++] = 0; /* end of name */

    packet[qpos++] = 0x00; packet[qpos++] = 0x01; /* QTYPE=A */
    packet[qpos++] = 0x00; packet[qpos++] = 0x01; /* QCLASS=IN */

    struct sockaddr_in dns_addr;
    memset(&dns_addr, 0, sizeof(dns_addr));
    dns_addr.sin_family = AF_INET;
    dns_addr.sin_port = htons(53);
    dns_addr.sin_addr.s_addr = dns_ip;

    ssize_t sent = sendto(sock, packet, qpos, 0, (struct sockaddr *)&dns_addr, sizeof(dns_addr));
    if (sent <= 0) {
        close(sock);
        return NULL;
    }

    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;

    int pr = poll(&pfd, 1, 2000);
    if (pr <= 0 || !(pfd.revents & POLLIN)) {
        close(sock);
        return NULL;
    }

    uint8_t resp[512];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t rlen = recvfrom(sock, resp, sizeof(resp), 0, (struct sockaddr *)&from_addr, &from_len);
    close(sock);

    if (rlen < 12) return NULL;
    if (resp[0] != 0x12 || resp[1] != 0x34) return NULL;
    uint16_t ancount = (uint16_t)((resp[6] << 8) | resp[7]);
    if (ancount == 0) return NULL;

    int pos = 12;
    while (pos < rlen && resp[pos] != 0) {
        pos += resp[pos] + 1;
    }
    pos += 5;

    for (uint16_t a = 0; a < ancount && pos < rlen; ++a) {
        if ((resp[pos] & 0xc0) == 0xc0) {
            pos += 2;
        } else {
            while (pos < rlen && resp[pos] != 0) {
                pos += resp[pos] + 1;
            }
            pos++;
        }
        if (pos + 10 > rlen) break;
        uint16_t atype = (uint16_t)((resp[pos] << 8) | resp[pos + 1]);
        uint16_t rdlen = (uint16_t)((resp[pos + 8] << 8) | resp[pos + 9]);
        pos += 10;
        if (atype == 1 && rdlen == 4 && pos + 4 <= rlen) {
            memcpy(&s_host_addr, resp + pos, 4);
            strncpy(s_host_name, name, sizeof(s_host_name) - 1);
            s_host_name[sizeof(s_host_name) - 1] = '\0';
            s_addr_list[0] = (char *)&s_host_addr;
            s_addr_list[1] = NULL;
            s_hostent.h_name = s_host_name;
            s_hostent.h_aliases = NULL;
            s_hostent.h_addrtype = AF_INET;
            s_hostent.h_length = sizeof(uint32_t);
            s_hostent.h_addr_list = s_addr_list;
            return &s_hostent;
        }
        pos += rdlen;
    }

    return NULL;
}

struct hostent *gethostbyaddr(const void *addr, socklen_t len, int type) {
    if (!addr || len < sizeof(uint32_t) || type != AF_INET) return NULL;
    uint32_t ip = *(const uint32_t *)addr;
    s_host_addr = ip;
    s_addr_list[0] = (char *)&s_host_addr;
    s_addr_list[1] = NULL;
    inet_ntop(AF_INET, addr, s_host_name, sizeof(s_host_name));
    s_hostent.h_name = s_host_name;
    s_hostent.h_aliases = NULL;
    s_hostent.h_addrtype = AF_INET;
    s_hostent.h_length = sizeof(uint32_t);
    s_hostent.h_addr_list = s_addr_list;
    return &s_hostent;
}

static char *duplicate_text(const char *value) {
    if (!value) return NULL;
    size_t length = strlen(value);
    char *copy = malloc(length + 1U);
    if (!copy) return NULL;
    memcpy(copy, value, length + 1U);
    return copy;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result) {
    if (!result || (!node && !service)) return EAI_BADFLAGS;
    *result = NULL;
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int socktype = hints ? hints->ai_socktype : 0;
    int protocol = hints ? hints->ai_protocol : 0;
    int flags = hints ? hints->ai_flags : 0;
    if (family != AF_UNSPEC && family != AF_INET) return EAI_FAMILY;
    if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
        return EAI_SOCKTYPE;
    if (protocol != 0 && protocol != IPPROTO_TCP && protocol != IPPROTO_UDP)
        return EAI_SERVICE;
    /* sockaddr_in stores addresses in network byte order.  The native
     * kernel's packet fields are little-endian host words, so use htonl for
     * the well-known host-order constants while preserving inet_addr()'s
     * already-normalized result for parsed names. */
    uint32_t address = (flags & AI_PASSIVE) ? htonl(INADDR_ANY) :
                                              htonl(INADDR_LOOPBACK);
    const char *canonical = node ? node : "0.0.0.0";
    if (node) {
        uint32_t parsed = inet_addr(node);
        if (parsed != 0U || strcmp(node, "0.0.0.0") == 0) {
            address = parsed;
        } else {
            if (flags & AI_NUMERICHOST) return EAI_NONAME;
            struct hostent *host = gethostbyname(node);
            if (!host || !host->h_addr_list || !host->h_addr_list[0])
                return EAI_AGAIN;
            memcpy(&address, host->h_addr_list[0], sizeof(address));
            canonical = host->h_name ? host->h_name : node;
        }
    }
    uint16_t port = 0;
    if (service && service[0] != '\0') {
        int parsed_port = atoi(service);
        if (parsed_port <= 0 || parsed_port > 65535) {
            if (flags & AI_NUMERICSERV) return EAI_SERVICE;
            return EAI_SERVICE;
        }
        port = htons((uint16_t)parsed_port);
    }
    struct addrinfo *info = calloc(1, sizeof(*info));
    struct sockaddr_in *sockaddr = calloc(1, sizeof(*sockaddr));
    if (!info || !sockaddr) {
        free(info);
        free(sockaddr);
        return EAI_MEMORY;
    }
    sockaddr->sin_family = AF_INET;
    sockaddr->sin_port = port;
    sockaddr->sin_addr.s_addr = address;
    info->ai_flags = flags;
    info->ai_family = AF_INET;
    info->ai_socktype = socktype;
    info->ai_protocol = protocol;
    info->ai_addrlen = sizeof(*sockaddr);
    info->ai_addr = (struct sockaddr *)sockaddr;
    info->ai_next = NULL;
    if (flags & AI_CANONNAME) {
        info->ai_canonname = duplicate_text(canonical);
        if (!info->ai_canonname) {
            free(sockaddr);
            free(info);
            return EAI_MEMORY;
        }
    }
    *result = info;
    return 0;
}

void freeaddrinfo(struct addrinfo *info) {
    while (info) {
        struct addrinfo *next = info->ai_next;
        free(info->ai_canonname);
        free(info->ai_addr);
        free(info);
        info = next;
    }
}

const char *gai_strerror(int error_code) {
    switch (error_code) {
        case 0: return "success";
        case EAI_BADFLAGS: return "invalid flags";
        case EAI_NONAME: return "name or service not known";
        case EAI_AGAIN: return "temporary name resolution failure";
        case EAI_FAIL: return "name resolution failure";
        case EAI_FAMILY: return "address family not supported";
        case EAI_SOCKTYPE: return "socket type not supported";
        case EAI_SERVICE: return "service not supported";
        case EAI_MEMORY: return "out of memory";
        default: return "unknown address-info error";
    }
}

int mount(const char *source, const char *target,
          const char *filesystemtype, unsigned long mountflags,
          const void *data) {
    if (!target) {
        errno = EFAULT;
        return -1;
    }
    struct akaos_mount_args margs;
    margs.source = source;
    margs.target = target;
    margs.fstype = filesystemtype;
    margs.flags = mountflags;
    margs.data = data;
    return (int)translate_result(syscall3(AKAOS_SYSCALL_MOUNT, (uint64_t)(uintptr_t)&margs, sizeof(margs), 0));
}

int umount(const char *target) {
    return umount2(target, 0);
}

int umount2(const char *target, int flags) {
    if (!target) {
        errno = EFAULT;
        return -1;
    }
    size_t len = strlen(target);
    if (len == 0 || len > AKAOS_SYSCALL_PATH_MAX) {
        errno = EINVAL;
        return -1;
    }
    return (int)translate_result(syscall3(AKAOS_SYSCALL_UMOUNT, (uint64_t)(uintptr_t)target, (uint64_t)len, (uint64_t)flags));
}

unsigned int sleep(unsigned int seconds) {
    if (seconds == 0) return 0;
    poll(NULL, 0, (int)(seconds * 1000));
    return 0;
}

int usleep(useconds_t usec) {
    int ms = (int)((usec + 999) / 1000);
    if (ms <= 0) ms = 1;
    return poll(NULL, 0, ms);
}
