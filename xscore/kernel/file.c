#include <xscore/file.h>
#include <xscore/serial.h>
#include <xscore/smp.h>
#include <xscore/vfs.h>
#include <xscore/process.h>
#include <xscore/tty.h>
#include <xscore/input.h>
#include <xscore/framebuffer.h>
#include <xscore/heap.h>
#include <xscore/thread.h>
#include <xscore/audio.h>
#include <xscore/net.h>
#include <xscore/socket.h>
#include <xscore/random.h>
#include <akaos/errno.h>
#include <akaos/fs.h>
#include <akaos/poll.h>
#include <akaos/display.h>
#include <akaos/ioctl.h>
#include <akaos/syscall.h>
#include <akaos/signalfd.h>
#include <akaos/termios.h>
#include <akaos/signal.h>
#include <stddef.h>
#include <stdint.h>

#define FILE_READABLE UINT32_C(1)
#define FILE_WRITABLE UINT32_C(2)
#define FILE_IMMORTAL UINT32_C(4)
#define FILE_MEMORY UINT32_C(8)
#define FILE_DIRECTORY UINT32_C(16)
#define FILE_VNODE UINT32_C(32)
#define FILE_CONSOLE UINT32_C(64)
#define FILE_PIPE UINT32_C(128)
#define FILE_APPEND UINT32_C(256)
#define FILE_SHM UINT32_C(512)
#define FILE_INPUT UINT32_C(1024)
#define FILE_FRAMEBUFFER UINT32_C(2048)
#define FILE_NULL UINT32_C(4096)
#define FILE_ZERO UINT32_C(8192)
#define FILE_PTY_MASTER UINT32_C(16384)
#define FILE_PTY_SLAVE UINT32_C(32768)
#define GENERATED_FILE_MAX_SIZE UINT64_C(4096)
#define FILE_GENERATED UINT32_C(65536)
#define FILE_AUDIO UINT32_C(131072)
#define FILE_NET UINT32_C(262144)
#define FILE_SOCKET UINT32_C(524288)
#define FILE_NONBLOCK UINT32_C(1048576)
#define FILE_RANDOM UINT32_C(2097152)
#define FILE_SIGNALFD UINT32_C(4194304)
/* Reserve two file objects per pipe and headroom for concurrent desktop
 * processes at the 1024-descriptor per-process limit. */
#define XSCORE_FILE_POOL_SIZE 2048
#define XSCORE_PIPE_POOL_SIZE 512
#define XSCORE_PIPE_BUF_SIZE 4096
#define XSCORE_PTY_MAX 4
#define XSCORE_PTY_BUF_SIZE 1024

struct xscore_pipe {
    uint8_t buffer[XSCORE_PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    uint32_t readers;
    uint32_t writers;
    int in_use;
};

static struct xscore_pipe pipe_pool[XSCORE_PIPE_POOL_SIZE];

struct xscore_pty_buffer {
    uint8_t data[XSCORE_PTY_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
};

struct xscore_pty {
    struct xscore_pty_buffer to_master;
    struct xscore_pty_buffer to_slave;
    uint32_t master_descriptions;
    uint32_t slave_descriptions;
    uint32_t number;
    struct akaos_termios termios;
    struct akaos_winsize winsize;
    uint8_t canonical[256];
    uint32_t canonical_length;
    int in_use;
};

static struct xscore_pty pty_pool[XSCORE_PTY_MAX];

static void pty_defaults(struct xscore_pty *pty) {
    pty->termios.c_iflag = AKAOS_ICRNL;
    pty->termios.c_oflag = AKAOS_OPOST | AKAOS_ONLCR;
    pty->termios.c_cflag = 0;
    pty->termios.c_lflag = AKAOS_ISIG | AKAOS_ICANON | AKAOS_ECHO |
                           AKAOS_ECHOE;
    pty->termios.c_cc[AKAOS_VINTR] = 3;
    pty->termios.c_cc[AKAOS_VQUIT] = 28;
    pty->termios.c_cc[AKAOS_VERASE] = 0x7f;
    pty->termios.c_cc[AKAOS_VKILL] = 21;
    pty->termios.c_cc[AKAOS_VEOF] = 4;
    pty->termios.c_cc[AKAOS_VMIN] = 1;
    pty->termios.c_cc[AKAOS_VTIME] = 0;
    pty->termios.c_cc[AKAOS_VSUSP] = 26;
    pty->winsize.ws_row = 25;
    pty->winsize.ws_col = 80;
    pty->winsize.ws_xpixel = 640;
    pty->winsize.ws_ypixel = 400;
    pty->canonical_length = 0;
}

struct xscore_file {
    uint32_t flags;
    uint32_t references;
    /* A descriptor reference keeps this object allocated; this lock
     * serializes offset and per-description state while that reference is
     * held.  It is deliberately separate from file_pool_lock so blocking I/O
     * never holds the global allocation lock. */
    xscore_spinlock_t operation_lock;
    uint64_t offset;
    const uint8_t *data;
    uint64_t size;
    uint32_t node_id;
    uint32_t flock_type;
    uint32_t signal_mask;
    struct xscore_pipe *pipe;
    struct xscore_pty *pty;
    uint8_t *generated;
};

static struct xscore_file console_input = {
    .flags = FILE_READABLE | FILE_IMMORTAL | FILE_CONSOLE,
    .references = 0,
    .offset = 0,
    .data = 0,
    .size = 0,
    .node_id = 0,
    .pipe = 0,
};
static struct xscore_file console_output = {
    .flags = FILE_WRITABLE | FILE_IMMORTAL,
    .references = 0,
    .offset = 0,
    .data = 0,
    .size = 0,
    .node_id = 0,
    .pipe = 0,
};
static struct xscore_file console_error = {
    .flags = FILE_WRITABLE | FILE_IMMORTAL,
    .references = 0,
    .offset = 0,
    .data = 0,
    .size = 0,
    .node_id = 0,
    .pipe = 0,
};
static struct xscore_file file_pool[XSCORE_FILE_POOL_SIZE];

/* SMP safety: the descriptor/file/pipe/PTY pools and reference counts mutate
 * in open/close/clone paths that can now run on any core. */
static xscore_spinlock_t file_pool_lock = XSCORE_SPINLOCK_INIT;
static int file_pool_locked;

/* Returns irqsave flags for file_pool_release (0 when locking disabled). */
static uintptr_t file_pool_acquire(void) {
    if (!file_pool_locked) return 0;
    return xscore_spin_lock_irqsave(&file_pool_lock);
}

static void file_pool_release(uintptr_t flags) {
    if (!file_pool_locked) return;
    xscore_spin_unlock_irqrestore(&file_pool_lock, flags);
}

static void file_acquire(struct xscore_file *file) {
    if (!file) return;
    if (file_pool_locked) {
        __atomic_add_fetch(&file->references, 1, __ATOMIC_ACQ_REL);
    } else {
        ++file->references;
    }
}

/* Caller must hold file_pool_lock (via file_pool_acquire). */
static void file_release(struct xscore_file *file) {
    if (!file || file->references == 0) return;
    if (file_pool_locked) {
        __atomic_sub_fetch(&file->references, 1, __ATOMIC_ACQ_REL);
    } else {
        --file->references;
    }
    if (file->references == 0 && (file->flags & FILE_IMMORTAL) == 0) {
        if (file->flags & FILE_PIPE) {
            struct xscore_pipe *pipe = file->pipe;
            if (pipe) {
                if (file->flags & FILE_READABLE) {
                    if (pipe->readers > 0) --pipe->readers;
                }
                if (file->flags & FILE_WRITABLE) {
                    if (pipe->writers > 0) --pipe->writers;
                }
                if (pipe->readers == 0 && pipe->writers == 0) {
                    pipe->in_use = 0;
                    pipe->count = 0;
                    pipe->read_pos = 0;
                    pipe->write_pos = 0;
                }
            }
        }
        if (file->flags & FILE_SHM) {
            xscore_process_shm_descriptor_release(file->node_id);
        }
        if (file->flags & (FILE_VNODE | FILE_DIRECTORY)) {
            xscore_vfs_node_release(file->node_id);
        }
        if (file->flags & (FILE_PTY_MASTER | FILE_PTY_SLAVE)) {
            if (file->pty) {
                if ((file->flags & FILE_PTY_MASTER) &&
                    file->pty->master_descriptions > 0) {
                    --file->pty->master_descriptions;
                }
                if ((file->flags & FILE_PTY_SLAVE) &&
                    file->pty->slave_descriptions > 0) {
                    --file->pty->slave_descriptions;
                }
                if (file->pty->master_descriptions == 0 &&
                    file->pty->slave_descriptions == 0) {
                    file->pty->in_use = 0;
                }
            }
        }
        if ((file->flags & (FILE_FRAMEBUFFER | FILE_WRITABLE)) ==
            (FILE_FRAMEBUFFER | FILE_WRITABLE)) {
            xscore_framebuffer_graphics_release();
        }
        if (file->flags & FILE_INPUT) xscore_input_client_close();
        if ((file->flags & FILE_GENERATED) && file->generated) {
            (void)xscore_heap_free(file->generated);
        }
        if (file->flags & FILE_SOCKET) {
            struct xscore_file *released_files[
                XSCORE_UNIX_RIGHTS_QUEUE * XSCORE_UNIX_RIGHTS_MAX];
            size_t released_count = xscore_socket_close_collect(
                (int)file->node_id, released_files,
                XSCORE_UNIX_RIGHTS_QUEUE * XSCORE_UNIX_RIGHTS_MAX);
            for (size_t index = 0U; index < released_count; ++index)
                file_release(released_files[index]);
        }
        file->flags = 0;
        file->offset = 0;
        file->data = 0;
        file->size = 0;
        file->node_id = 0;
        file->flock_type = 0;
        file->pipe = 0;
        file->pty = 0;
        file->generated = 0;
    }
}

static uintptr_t fd_table_acquire(struct xscore_fd_table *table) {
    if (!table) return 0;
    return xscore_spin_lock_irqsave(&table->lock);
}

static void fd_table_release(struct xscore_fd_table *table, uintptr_t flags) {
    if (!table) return;
    xscore_spin_unlock_irqrestore(&table->lock, flags);
}

static void fd_table_storage_reset(struct xscore_fd_table *table) {
    for (uint32_t index = 0; index < XSCORE_FD_INLINE_MAX; ++index) {
        table->inline_entries[index] = 0;
        table->inline_descriptor_flags[index] = 0U;
    }
    table->entries = table->inline_entries;
    table->descriptor_flags = table->inline_descriptor_flags;
    table->capacity = XSCORE_FD_INLINE_MAX;
}

/* Caller holds the table lock. Growing the backing arrays keeps kernel-stack
 * helper tables small while allowing long-lived userspace processes to use a
 * practical descriptor range. */
static int fd_table_grow_locked(struct xscore_fd_table *table,
                                uint32_t required_capacity) {
    if (!table || required_capacity > XSCORE_FD_MAX)
        return -AKAOS_EMFILE;
    if (required_capacity <= table->capacity) return 0;

    uint32_t capacity = table->capacity ? table->capacity : XSCORE_FD_INLINE_MAX;
    while (capacity < required_capacity && capacity < XSCORE_FD_MAX) {
        capacity = capacity > XSCORE_FD_MAX / 2U
                       ? XSCORE_FD_MAX
                       : capacity * 2U;
    }
    if (capacity < required_capacity) return -AKAOS_EMFILE;

    struct xscore_file **entries = xscore_heap_allocate(
        (size_t)capacity * sizeof(*entries));
    if (!entries) return -AKAOS_ENOMEM;
    uint32_t *descriptor_flags = xscore_heap_allocate(
        (size_t)capacity * sizeof(*descriptor_flags));
    if (!descriptor_flags) {
        (void)xscore_heap_free(entries);
        return -AKAOS_ENOMEM;
    }
    for (uint32_t index = 0; index < capacity; ++index) {
        entries[index] = 0;
        descriptor_flags[index] = 0U;
    }
    for (uint32_t index = 0; index < table->capacity; ++index) {
        entries[index] = table->entries[index];
        descriptor_flags[index] = table->descriptor_flags[index];
    }
    if (table->entries && table->entries != table->inline_entries)
        (void)xscore_heap_free(table->entries);
    if (table->descriptor_flags &&
        table->descriptor_flags != table->inline_descriptor_flags)
        (void)xscore_heap_free(table->descriptor_flags);
    table->entries = entries;
    table->descriptor_flags = descriptor_flags;
    table->capacity = capacity;
    return 0;
}

/* Return a referenced file description.  The table and pool locks are held
 * only while taking the reference; callers must eventually call
 * file_put().  This closes the close-vs-I/O use-after-free window without
 * holding a global lock across potentially blocking device operations. */
static struct xscore_file *fd_lookup_ref(struct xscore_fd_table *table,
                                         int descriptor) {
    if (!table || descriptor < 0 || descriptor >= XSCORE_FD_MAX) return 0;
    uintptr_t pool_flags = file_pool_acquire();
    uintptr_t table_flags = fd_table_acquire(table);
    struct xscore_file *file = (uint32_t)descriptor < table->capacity
                                    ? table->entries[descriptor]
                                    : 0;
    if (file) file_acquire(file);
    fd_table_release(table, table_flags);
    file_pool_release(pool_flags);
    return file;
}

static void file_put(struct xscore_file *file) {
    if (!file) return;
    uintptr_t pool_flags = file_pool_acquire();
    file_release(file);
    file_pool_release(pool_flags);
}

static uintptr_t file_operation_acquire(struct xscore_file *file) {
    return file ? xscore_spin_lock_irqsave(&file->operation_lock) : 0;
}

static void file_operation_release(struct xscore_file *file, uintptr_t flags) {
    if (file) xscore_spin_unlock_irqrestore(&file->operation_lock, flags);
}

/* Caller must hold file_pool_lock (via file_pool_acquire). */
static int fd_install_with_flags(struct xscore_fd_table *table,
                                 struct xscore_file *file,
                                 uint32_t descriptor_flags) {
    if (!table || !file) return -AKAOS_EBADF;
    uintptr_t table_flags = fd_table_acquire(table);
    for (;;) {
        for (int descriptor = 0; descriptor < (int)table->capacity;
             ++descriptor) {
            if (!table->entries[descriptor]) {
                if (file->references == 0)
                    xscore_spinlock_init(&file->operation_lock);
                table->entries[descriptor] = file;
                table->descriptor_flags[descriptor] = descriptor_flags;
                file_acquire(file);
                fd_table_release(table, table_flags);
                return descriptor;
            }
        }
        if (table->capacity >= XSCORE_FD_MAX) break;
        uint32_t next_capacity = table->capacity > XSCORE_FD_MAX / 2U
                                     ? XSCORE_FD_MAX
                                     : table->capacity * 2U;
        int error = fd_table_grow_locked(table, next_capacity);
        if (error != 0) {
            fd_table_release(table, table_flags);
            return error;
        }
    }
    fd_table_release(table, table_flags);
    return -AKAOS_EMFILE;
}

static int fd_install(struct xscore_fd_table *table, struct xscore_file *file) {
    return fd_install_with_flags(table, file, 0U);
}

int64_t xscore_fd_open_memory(struct xscore_fd_table *table,
                              const uint8_t *data, uint64_t size) {
    if (!table || (!data && size != 0)) return -AKAOS_EBADF;
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = 0;
    for (uint32_t index = 0; index < XSCORE_FILE_POOL_SIZE; ++index) {
        if (file_pool[index].references == 0 && file_pool[index].flags == 0) {
            file = &file_pool[index];
            break;
        }
    }
    if (!file) {
        file_pool_release(pool_flags);
        return -AKAOS_ENFILE;
    }
    file->flags = FILE_READABLE | FILE_MEMORY;
    file->references = 0;
    file->offset = 0;
    file->data = data;
    file->size = size;
    file->node_id = 0;
    int descriptor = fd_install(table, file);
    file_pool_release(pool_flags);
    if (descriptor < 0) {
        file->flags = 0;
        file->data = 0;
        file->size = 0;
    }
    return descriptor;
}

int64_t xscore_fd_open_generated(struct xscore_fd_table *table,
                                 const uint8_t *data, uint64_t size) {
    if (!table || !data || size > GENERATED_FILE_MAX_SIZE) {
        return -AKAOS_EINVAL;
    }
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = 0;
    for (uint32_t index = 0; index < XSCORE_FILE_POOL_SIZE; ++index) {
        if (file_pool[index].references == 0 && file_pool[index].flags == 0) {
            file = &file_pool[index];
            break;
        }
    }
    if (!file) {
        file_pool_release(pool_flags);
        return -AKAOS_ENFILE;
    }
    file->generated = xscore_heap_allocate((size_t)(size ? size : 1));
    if (!file->generated) {
        file_pool_release(pool_flags);
        return -AKAOS_ENOMEM;
    }
    for (uint64_t index = 0; index < size; ++index) {
        file->generated[index] = data[index];
    }
    file->flags = FILE_READABLE | FILE_MEMORY | FILE_GENERATED;
    file->references = 0;
    file->offset = 0;
    file->data = file->generated;
    file->size = size;
    file->node_id = 0;
    file->pipe = 0;
    file->pty = 0;
    int descriptor = fd_install(table, file);
    file_pool_release(pool_flags);
    if (descriptor < 0) {
        (void)xscore_heap_free(file->generated);
        file->flags = 0;
        file->data = 0;
        file->size = 0;
        file->generated = 0;
    }
    return descriptor;
}

int64_t xscore_fd_open_directory(struct xscore_fd_table *table,
                                 uint32_t node_id) {
    if (!table) return -AKAOS_EBADF;
    int node_reference = 0;
    if (node_id < XSCORE_VFS_MAX_NODES) {
        node_reference = xscore_vfs_node_acquire(node_id);
        if (!node_reference) return -AKAOS_ENOENT;
    }
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = 0;
    for (uint32_t index = 0; index < XSCORE_FILE_POOL_SIZE; ++index) {
        if (file_pool[index].references == 0 && file_pool[index].flags == 0) {
            file = &file_pool[index];
            break;
        }
    }
    if (!file) {
        file_pool_release(pool_flags);
        if (node_reference) xscore_vfs_node_release(node_id);
        return -AKAOS_ENFILE;
    }
    file->flags = FILE_READABLE | FILE_DIRECTORY;
    file->references = 0;
    file->offset = 0;
    file->data = 0;
    file->size = 0;
    file->node_id = node_id;
    file->flock_type = 0;
    int descriptor = fd_install(table, file);
    file_pool_release(pool_flags);
    if (descriptor < 0) {
        file->flags = 0;
        file->node_id = 0;
        if (node_reference) xscore_vfs_node_release(node_id);
    }
    return descriptor;
}

int64_t xscore_fd_open_vnode(struct xscore_fd_table *table,
                             uint32_t node_id, uint64_t open_flags) {
    if (!table) return -AKAOS_EBADF;
    if (!xscore_vfs_node_acquire(node_id)) return -AKAOS_ENOENT;
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = 0;
    for (uint32_t index = 0; index < XSCORE_FILE_POOL_SIZE; ++index) {
        if (file_pool[index].references == 0 && file_pool[index].flags == 0) {
            file = &file_pool[index];
            break;
        }
    }
    if (!file) {
        file_pool_release(pool_flags);
        xscore_vfs_node_release(node_id);
        return -AKAOS_ENFILE;
    }
    file->flags = FILE_VNODE;
    if ((open_flags & 3) == AKAOS_O_WRONLY) {
        file->flags |= FILE_WRITABLE;
    } else if ((open_flags & 3) == AKAOS_O_RDWR) {
        file->flags |= (FILE_READABLE | FILE_WRITABLE);
    } else {
        file->flags |= FILE_READABLE;
    }
    if (open_flags & AKAOS_O_APPEND) {
        file->flags |= FILE_APPEND;
    }
    file->references = 0;
    file->offset = 0;
    file->data = 0;
    file->size = 0;
    file->node_id = node_id;
    file->flock_type = 0;
    int descriptor = fd_install(table, file);
    file_pool_release(pool_flags);
    if (descriptor < 0) {
        file->flags = 0;
        file->node_id = 0;
        xscore_vfs_node_release(node_id);
    }
    return descriptor;
}

int xscore_fd_directory_state(struct xscore_fd_table *table, int descriptor,
                              uint32_t *node_id, uint64_t *offset) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return 0;
    uintptr_t operation_flags = file_operation_acquire(file);
    if ((file->flags & FILE_DIRECTORY) == 0) {
        file_operation_release(file, operation_flags);
        file_put(file);
        return 0;
    }
    if (node_id) *node_id = file->node_id;
    if (offset) *offset = file->offset;
    file_operation_release(file, operation_flags);
    file_put(file);
    return 1;
}

int xscore_fd_directory_set_offset(struct xscore_fd_table *table,
                                   int descriptor, uint64_t offset) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return 0;
    uintptr_t operation_flags = file_operation_acquire(file);
    if ((file->flags & FILE_DIRECTORY) == 0) {
        file_operation_release(file, operation_flags);
        file_put(file);
        return 0;
    }
    file->offset = offset;
    file_operation_release(file, operation_flags);
    file_put(file);
    return 1;
}

int xscore_fd_vnode_state(struct xscore_fd_table *table, int descriptor,
                          uint32_t *node_id, int *readable, int *writable) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return 0;
    uintptr_t operation_flags = file_operation_acquire(file);
    if ((file->flags & FILE_VNODE) == 0) {
        file_operation_release(file, operation_flags);
        file_put(file);
        return 0;
    }
    if (node_id) *node_id = file->node_id;
    if (readable) *readable = (file->flags & FILE_READABLE) != 0;
    if (writable) *writable = (file->flags & FILE_WRITABLE) != 0;
    file_operation_release(file, operation_flags);
    file_put(file);
    return 1;
}

int64_t xscore_fd_open_shm(struct xscore_fd_table *table, uint32_t object_id,
                           int readable, int writable,
                           uint32_t descriptor_flags) {
    if (!table || (!readable && !writable)) return -AKAOS_EBADF;
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = 0;
    for (uint32_t index = 0; index < XSCORE_FILE_POOL_SIZE; ++index) {
        if (file_pool[index].references == 0 && file_pool[index].flags == 0) {
            file = &file_pool[index];
            break;
        }
    }
    if (!file) return -AKAOS_ENFILE;
    file->flags = FILE_SHM;
    if (readable) file->flags |= FILE_READABLE;
    if (writable) file->flags |= FILE_WRITABLE;
    file->references = 0;
    file->offset = 0;
    file->data = 0;
    file->size = 0;
    file->node_id = object_id;
    file->pipe = 0;
    int descriptor = fd_install_with_flags(table, file, descriptor_flags);
    file_pool_release(pool_flags);
    if (descriptor < 0) {
        file->flags = 0;
        file->node_id = 0;
    }
    return descriptor;
}

int64_t xscore_fd_open_input(struct xscore_fd_table *table) {
    if (!table) return -AKAOS_EBADF;
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = 0;
    for (uint32_t index = 0; index < XSCORE_FILE_POOL_SIZE; ++index) {
        if (file_pool[index].references == 0 && file_pool[index].flags == 0) {
            file = &file_pool[index];
            break;
        }
    }
    if (!file) return -AKAOS_ENFILE;
    file->flags = FILE_READABLE | FILE_INPUT;
    file->references = 0;
    file->offset = xscore_input_open_cursor();
    file->data = 0;
    file->size = 0;
    file->node_id = 0;
    file->pipe = 0;
    int descriptor = fd_install(table, file);
    file_pool_release(pool_flags);
    if (descriptor < 0) {
        file->flags = 0;
        file->offset = 0;
    } else {
        xscore_input_client_open();
    }
    return descriptor;
}

int64_t xscore_fd_open_framebuffer(struct xscore_fd_table *table,
                                   int readable, int writable) {
    struct akaos_display_info info;
    if (!table) return -AKAOS_EBADF;
    if (!xscore_framebuffer_get_info(&info)) return -AKAOS_ENODEV;
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = 0;
    for (uint32_t index = 0; index < XSCORE_FILE_POOL_SIZE; ++index) {
        if (file_pool[index].references == 0 && file_pool[index].flags == 0) {
            file = &file_pool[index];
            break;
        }
    }
    if (!file) {
        file_pool_release(pool_flags);
        return -AKAOS_ENFILE;
    }
    file->flags = FILE_FRAMEBUFFER;
    if (readable) file->flags |= FILE_READABLE;
    if (writable) file->flags |= FILE_WRITABLE;
    file->references = 0;
    file->offset = 0;
    file->data = 0;
    file->size = info.byte_size;
    file->node_id = 0;
    file->pipe = 0;
    int descriptor = fd_install(table, file);
    file_pool_release(pool_flags);
    if (descriptor < 0) {
        file->flags = 0;
        file->size = 0;
        return descriptor;
    }
    if (writable) xscore_framebuffer_graphics_acquire();
    return descriptor;
}

static int64_t fd_open_character(struct xscore_fd_table *table,
                                 uint32_t kind, int readable, int writable) {
    if (!table || (!readable && !writable)) return -AKAOS_EBADF;
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = 0;
    for (uint32_t index = 0; index < XSCORE_FILE_POOL_SIZE; ++index) {
        if (file_pool[index].references == 0 && file_pool[index].flags == 0) {
            file = &file_pool[index];
            break;
        }
    }
    if (!file) {
        file_pool_release(pool_flags);
        return -AKAOS_ENFILE;
    }
    file->flags = kind;
    if (readable) file->flags |= FILE_READABLE;
    if (writable) file->flags |= FILE_WRITABLE;
    file->references = 0;
    file->offset = 0;
    file->data = 0;
    file->size = 0;
    file->node_id = 0;
    file->pipe = 0;
    int descriptor = fd_install(table, file);
    file_pool_release(pool_flags);
    if (descriptor < 0) file->flags = 0;
    return descriptor;
}

int64_t xscore_fd_open_tty(struct xscore_fd_table *table,
                           int readable, int writable) {
    return fd_open_character(table, FILE_CONSOLE, readable, writable);
}

int64_t xscore_fd_open_null(struct xscore_fd_table *table,
                            int readable, int writable) {
    return fd_open_character(table, FILE_NULL, readable, writable);
}

int64_t xscore_fd_open_zero(struct xscore_fd_table *table,
                            int readable, int writable) {
    return fd_open_character(table, FILE_ZERO, readable, writable);
}

int64_t xscore_fd_open_random(struct xscore_fd_table *table) {
    return fd_open_character(table, FILE_RANDOM, 1, 0);
}

int64_t xscore_fd_open_audio(struct xscore_fd_table *table,
                             int readable, int writable) {
    return fd_open_character(table, FILE_AUDIO, readable, writable);
}

int64_t xscore_fd_open_net(struct xscore_fd_table *table,
                           int readable, int writable) {
    return fd_open_character(table, FILE_NET, readable, writable);
}

static struct xscore_file *file_allocate(void) {
    for (uint32_t index = 0; index < XSCORE_FILE_POOL_SIZE; ++index) {
        if (file_pool[index].references == 0 && file_pool[index].flags == 0) {
            file_pool[index].pty = 0;
            return &file_pool[index];
        }
    }
    return 0;
}

_Static_assert(sizeof(struct signalfd_siginfo) == 128U,
               "signalfd records must retain the Linux 128-byte ABI");

int64_t xscore_fd_signalfd(struct xscore_fd_table *table, int descriptor,
                           uint32_t signal_mask, uint32_t flags) {
    if (!table || (flags & ~(AKAOS_SFD_CLOEXEC | AKAOS_SFD_NONBLOCK)) != 0U) {
        return -AKAOS_EINVAL;
    }
    signal_mask &= ~((UINT32_C(1) << AKAOS_SIGKILL) |
                     (UINT32_C(1) << AKAOS_SIGSTOP));

    if (descriptor >= 0) {
        struct xscore_file *file = fd_lookup_ref(table, descriptor);
        if (!file) return -AKAOS_EBADF;
        uintptr_t operation_flags = file_operation_acquire(file);
        if ((file->flags & FILE_SIGNALFD) == 0U) {
            file_operation_release(file, operation_flags);
            file_put(file);
            return -AKAOS_EINVAL;
        }
        file->signal_mask = signal_mask;
        if (flags & AKAOS_SFD_NONBLOCK) file->flags |= FILE_NONBLOCK;
        else file->flags &= ~FILE_NONBLOCK;
        file_operation_release(file, operation_flags);
        file_put(file);

        uintptr_t table_flags = fd_table_acquire(table);
        if (table->entries[descriptor] == file) {
            if (flags & AKAOS_SFD_CLOEXEC)
                table->descriptor_flags[descriptor] |= AKAOS_FD_CLOEXEC;
            else
                table->descriptor_flags[descriptor] &= ~AKAOS_FD_CLOEXEC;
        } else {
            fd_table_release(table, table_flags);
            return -AKAOS_EBADF;
        }
        fd_table_release(table, table_flags);
        return descriptor;
    }
    if (descriptor != -1) return -AKAOS_EBADF;

    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = file_allocate();
    if (!file) {
        file_pool_release(pool_flags);
        return -AKAOS_ENFILE;
    }
    file->flags = FILE_READABLE | FILE_SIGNALFD;
    if (flags & AKAOS_SFD_NONBLOCK) file->flags |= FILE_NONBLOCK;
    file->references = 0;
    file->offset = 0;
    file->data = 0;
    file->size = 0;
    file->node_id = 0;
    file->signal_mask = signal_mask;
    file->pipe = 0;
    file->pty = 0;
    file->generated = 0;

    int new_descriptor = fd_install(table, file);
    if (new_descriptor >= 0 && (flags & AKAOS_SFD_CLOEXEC)) {
        uintptr_t table_flags = fd_table_acquire(table);
        table->descriptor_flags[new_descriptor] |= AKAOS_FD_CLOEXEC;
        fd_table_release(table, table_flags);
    }
    if (new_descriptor < 0) file->flags = 0;
    file_pool_release(pool_flags);
    return new_descriptor;
}

int64_t xscore_fd_open_socket(struct xscore_fd_table *table, int sock_id) {
    if (!table || sock_id < 0) return -AKAOS_EBADF;
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = file_allocate();
    if (!file) {
        file_pool_release(pool_flags);
        return -AKAOS_ENFILE;
    }

    file->flags = FILE_READABLE | FILE_WRITABLE | FILE_SOCKET;
    file->references = 0;
    file->offset = 0;
    file->data = 0;
    file->size = 0;
    file->node_id = (uint32_t)sock_id;
    file->pipe = 0;
    file->pty = 0;
    file->generated = 0;

    int descriptor = fd_install(table, file);
    if (descriptor < 0) {
        file_release(file);
    }
    file_pool_release(pool_flags);
    return descriptor;
}

int xscore_fd_socket_id(struct xscore_fd_table *table, int descriptor) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return -1;
    uintptr_t operation_flags = file_operation_acquire(file);
    int result = (file->flags & FILE_SOCKET) ? (int)file->node_id : -1;
    file_operation_release(file, operation_flags);
    file_put(file);
    return result;
}

int xscore_fd_socket_acquire(struct xscore_fd_table *table, int descriptor,
                             int *sock_id,
                             struct xscore_file **reference) {
    if (!table || !sock_id || !reference) return -AKAOS_EBADF;
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return -AKAOS_EBADF;
    uintptr_t operation_flags = file_operation_acquire(file);
    int is_socket = (file->flags & FILE_SOCKET) != 0U;
    int id = (int)file->node_id;
    file_operation_release(file, operation_flags);
    if (!is_socket || id < 0 || id >= XSCORE_SOCKET_MAX) {
        file_put(file);
        return -AKAOS_ENOTSOCK;
    }
    *sock_id = id;
    *reference = file;
    return 0;
}

int xscore_fd_transfer_acquire(struct xscore_fd_table *table, int descriptor,
                               struct xscore_file **transfer) {
    if (!table || !transfer || descriptor < 0 ||
        descriptor >= XSCORE_FD_MAX) return -AKAOS_EBADF;
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return -AKAOS_EBADF;
    uintptr_t operation_flags = file_operation_acquire(file);
    uint32_t flags = file->flags;
    file_operation_release(file, operation_flags);
    /* Socket-to-socket reference cycles require Unix-socket garbage
     * collection, which this first descriptor-passing tranche does not yet
     * provide. Other open-file descriptions retain their normal shared
     * offset/resource semantics across SCM_RIGHTS. */
    if (flags & FILE_SOCKET) {
        file_put(file);
        return -AKAOS_EOPNOTSUPP;
    }
    *transfer = file;
    return 0;
}

int64_t xscore_fd_transfer_install_many(
    struct xscore_fd_table *table, struct xscore_file *const *transfers,
    size_t count, int *descriptors) {
    if (!table || (count != 0U && (!transfers || !descriptors)) ||
        count > XSCORE_UNIX_RIGHTS_MAX)
        return -AKAOS_EINVAL;
    for (size_t index = 0U; index < count; ++index)
        if (!transfers[index]) return -AKAOS_EINVAL;

    uintptr_t pool_flags = file_pool_acquire();
    uintptr_t table_flags = fd_table_acquire(table);
    int slots[XSCORE_UNIX_RIGHTS_MAX];
    size_t found = 0U;
    for (int descriptor = 0; descriptor < (int)table->capacity && found < count;
         ++descriptor) {
        if (!table->entries[descriptor]) slots[found++] = descriptor;
    }
    while (found < count && table->capacity < XSCORE_FD_MAX) {
        uint32_t next_capacity = table->capacity > XSCORE_FD_MAX / 2U
                                     ? XSCORE_FD_MAX
                                     : table->capacity * 2U;
        int error = fd_table_grow_locked(table, next_capacity);
        if (error != 0) {
            fd_table_release(table, table_flags);
            file_pool_release(pool_flags);
            return error;
        }
        for (int descriptor = (int)(next_capacity / 2U);
             descriptor < (int)table->capacity && found < count;
             ++descriptor) {
            if (!table->entries[descriptor]) slots[found++] = descriptor;
        }
    }
    if (found != count) {
        fd_table_release(table, table_flags);
        file_pool_release(pool_flags);
        return -AKAOS_EMFILE;
    }
    for (size_t index = 0U; index < count; ++index) {
        struct xscore_file *file = transfers[index];
        if (file->references == 0U) xscore_spinlock_init(&file->operation_lock);
        table->entries[slots[index]] = file;
        file_acquire(file);
        descriptors[index] = slots[index];
    }
    fd_table_release(table, table_flags);
    file_pool_release(pool_flags);
    return (int64_t)count;
}

void xscore_fd_transfer_release(struct xscore_file *transfer) {
    file_put(transfer);
}

int64_t xscore_fd_open_ptmx(struct xscore_fd_table *table) {
    if (!table) return -AKAOS_EBADF;
    struct xscore_pty *pty = 0;
    for (uint32_t index = 0; index < XSCORE_PTY_MAX; ++index) {
        if (!pty_pool[index].in_use) {
            pty = &pty_pool[index];
            pty->in_use = 1;
            pty->number = index;
            pty->master_descriptions = 1;
            pty->slave_descriptions = 0;
            pty->to_master.read_pos = pty->to_master.write_pos = 0;
            pty->to_master.count = 0;
            pty->to_slave.read_pos = pty->to_slave.write_pos = 0;
            pty->to_slave.count = 0;
            pty_defaults(pty);
            break;
        }
    }
    if (!pty) return -AKAOS_EAGAIN;
    char path[] = "/dev/pts/0";
    path[9] = (char)('0' + pty->number);
    struct akaos_stat status;
    if (xscore_vfs_stat_at(0, path, &status) != 0 &&
        !xscore_vfs_add_character_device(path, 0620)) {
        pty->in_use = 0;
        return -AKAOS_ENFILE;
    }
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = file_allocate();
    if (!file) {
        file_pool_release(pool_flags);
        pty->in_use = 0;
        return -AKAOS_ENFILE;
    }
    file->flags = FILE_READABLE | FILE_WRITABLE | FILE_PTY_MASTER;
    file->references = 0;
    file->offset = 0;
    file->data = 0;
    file->size = 0;
    file->node_id = pty->number;
    file->pipe = 0;
    file->pty = pty;
    int descriptor = fd_install(table, file);
    if (descriptor < 0) {
        file->flags = 0;
        file->pty = 0;
        pty->in_use = 0;
    }
    file_pool_release(pool_flags);
    return descriptor;
}

int64_t xscore_fd_open_pts(struct xscore_fd_table *table, uint32_t number,
                           int readable, int writable) {
    if (!table || number >= XSCORE_PTY_MAX || !pty_pool[number].in_use ||
        (!readable && !writable)) return -AKAOS_ENODEV;
    uintptr_t pool_flags = file_pool_acquire();
    struct xscore_file *file = file_allocate();
    if (!file) {
        file_pool_release(pool_flags);
        return -AKAOS_ENFILE;
    }
    file->flags = FILE_PTY_SLAVE;
    if (readable) file->flags |= FILE_READABLE;
    if (writable) file->flags |= FILE_WRITABLE;
    file->references = 0;
    file->offset = 0;
    file->data = 0;
    file->size = 0;
    file->node_id = number;
    file->pipe = 0;
    file->pty = &pty_pool[number];
    ++pty_pool[number].slave_descriptions;
    int descriptor = fd_install(table, file);
    if (descriptor < 0) {
        --pty_pool[number].slave_descriptions;
        file->flags = 0;
        file->pty = 0;
    }
    file_pool_release(pool_flags);
    return descriptor;
}

int xscore_fd_pts_number(struct xscore_fd_table *table, int descriptor,
                         uint32_t *number) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return 0;
    uintptr_t operation_flags = file_operation_acquire(file);
    int result = 0;
    if ((file->flags & FILE_PTY_MASTER) != 0 && file->pty) {
        if (number) *number = file->pty->number;
        result = 1;
    }
    file_operation_release(file, operation_flags);
    file_put(file);
    return result;
}

int xscore_fd_is_tty(struct xscore_fd_table *table, int descriptor) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return 0;
    uintptr_t operation_flags = file_operation_acquire(file);
    int result = (file->flags & (FILE_CONSOLE | FILE_PTY_MASTER |
                                 FILE_PTY_SLAVE)) != 0;
    file_operation_release(file, operation_flags);
    file_put(file);
    return result;
}

int64_t xscore_fd_ioctl(struct xscore_fd_table *table, int descriptor,
                        uint32_t request, void *argument) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return -AKAOS_EBADF;
    if (!argument) {
        file_put(file);
        return -AKAOS_EBADF;
    }
    uintptr_t operation_flags = file_operation_acquire(file);
    if (request == AKAOS_FIONREAD) {
        if ((file->flags & FILE_PIPE) == 0 || !file->pipe) {
            file_operation_release(file, operation_flags);
            file_put(file);
            return -AKAOS_ENOTTY;
        }
        *(int *)argument = (int)file->pipe->count;
        file_operation_release(file, operation_flags);
        file_put(file);
        return 0;
    }
    if (file->flags & FILE_NET) {
        if (request == AKAOS_SIOCGIFSTATS) {
            xscore_net_get_stats((struct akaos_net_stats *)argument);
            file_operation_release(file, operation_flags);
            file_put(file);
            return 0;
        }
        file_operation_release(file, operation_flags);
        file_put(file);
        return -AKAOS_EINVAL;
    }
    int console = (file->flags & FILE_CONSOLE) != 0;
    int pseudo = (file->flags & (FILE_PTY_MASTER | FILE_PTY_SLAVE)) != 0;
    if (!console && !pseudo) {
        file_operation_release(file, operation_flags);
        file_put(file);
        return -AKAOS_ENOTTY;
    }

    if (request == AKAOS_TCGETS) {
        if (console) xscore_tty_get_termios(argument);
        else *(struct akaos_termios *)argument = file->pty->termios;
        file_operation_release(file, operation_flags);
        file_put(file);
        return 0;
    }
    if (request == AKAOS_TCSETS) {
        if (console) xscore_tty_set_termios(argument);
        else file->pty->termios = *(const struct akaos_termios *)argument;
        file_operation_release(file, operation_flags);
        file_put(file);
        return 0;
    }
    if (request == AKAOS_TIOCGWINSZ) {
        struct akaos_winsize *size = argument;
        if (pseudo) {
            *size = file->pty->winsize;
        } else {
            struct akaos_display_info info;
            size->ws_col = 80;
            size->ws_row = 25;
            size->ws_xpixel = 640;
            size->ws_ypixel = 400;
            if (xscore_framebuffer_get_info(&info)) {
                size->ws_xpixel = (uint16_t)info.width;
                size->ws_ypixel = (uint16_t)info.height;
                size->ws_col = (uint16_t)(info.width / 8U);
                size->ws_row = (uint16_t)(info.height / 16U);
            }
        }
        file_operation_release(file, operation_flags);
        file_put(file);
        return 0;
    }
    if (request == AKAOS_TIOCSWINSZ) {
        if (pseudo) file->pty->winsize = *(const struct akaos_winsize *)argument;
        file_operation_release(file, operation_flags);
        file_put(file);
        return 0;
    }
    if (request == AKAOS_TIOCGNAME) {
        char *name = argument;
        const char *source = console ? "/dev/tty1" :
            ((file->flags & FILE_PTY_MASTER) ? "/dev/ptmx" : "/dev/pts/0");
        uint32_t index = 0;
        while (source[index] && index + 1 < AKAOS_TTY_NAME_MAX) {
            name[index] = source[index];
            ++index;
        }
        if (file->flags & FILE_PTY_SLAVE) name[9] = (char)('0' + file->pty->number);
        name[index] = 0;
        file_operation_release(file, operation_flags);
        file_put(file);
        return 0;
    }
    file_operation_release(file, operation_flags);
    file_put(file);
    return -AKAOS_EINVAL;
}

int64_t xscore_fd_flock(struct xscore_fd_table *table, int descriptor,
                        int operation) {
    const int operation_mask = AKAOS_LOCK_SH | AKAOS_LOCK_EX | AKAOS_LOCK_UN;
    const int allowed_mask = operation_mask | AKAOS_LOCK_NB;
    int requested = operation & operation_mask;
    if (operation < 0 || (operation & ~allowed_mask) != 0 ||
        (requested != AKAOS_LOCK_SH && requested != AKAOS_LOCK_EX &&
         requested != AKAOS_LOCK_UN)) return -AKAOS_EINVAL;
    if (!table || descriptor < 0 || descriptor >= XSCORE_FD_MAX)
        return -AKAOS_EBADF;

    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return -AKAOS_EBADF;
    int64_t result = 0;
    for (;;) {
        uintptr_t operation_flags = file_operation_acquire(file);
        if ((file->flags & FILE_VNODE) == 0) {
            file_operation_release(file, operation_flags);
            result = -AKAOS_EINVAL;
            break;
        }
        uint32_t key_id = file->node_id;
        uintptr_t pool_flags = file_pool_acquire();
        int conflict = 0;
        if (requested != AKAOS_LOCK_UN) {
            for (uint32_t index = 0; index < XSCORE_FILE_POOL_SIZE; ++index) {
                struct xscore_file *other = &file_pool[index];
                if (other == file || other->references == 0 ||
                    (other->flags & FILE_VNODE) == 0 ||
                    other->node_id != key_id ||
                    (other->flock_type != AKAOS_LOCK_SH &&
                     other->flock_type != AKAOS_LOCK_EX)) continue;
                if (requested == AKAOS_LOCK_EX ||
                    other->flock_type == AKAOS_LOCK_EX) {
                    conflict = 1;
                    break;
                }
            }
        }
        if (!conflict) file->flock_type = (uint32_t)requested;
        file_pool_release(pool_flags);
        file_operation_release(file, operation_flags);
        if (!conflict) break;
        if (operation & AKAOS_LOCK_NB) {
            result = -AKAOS_EAGAIN;
            break;
        }
        if (xscore_process_signal_pending_current()) {
            result = -AKAOS_EINTR;
            break;
        }
        xscore_thread_yield();
    }
    file_put(file);
    return result;
}

int xscore_fd_shm_state(struct xscore_fd_table *table, int descriptor,
                        uint32_t *object_id, int *readable, int *writable) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return 0;
    uintptr_t operation_flags = file_operation_acquire(file);
    if ((file->flags & FILE_SHM) == 0) {
        file_operation_release(file, operation_flags);
        file_put(file);
        return 0;
    }
    if (object_id) *object_id = file->node_id;
    if (readable) *readable = (file->flags & FILE_READABLE) != 0;
    if (writable) *writable = (file->flags & FILE_WRITABLE) != 0;
    file_operation_release(file, operation_flags);
    file_put(file);
    return 1;
}

static int64_t pty_buffer_read(struct xscore_pty_buffer *queue,
                               uint32_t peer_descriptions, void *buffer,
                               uint64_t length) {
    while (queue->count == 0) {
        if (peer_descriptions == 0) return 0;
        if (xscore_process_signal_pending_current()) return -AKAOS_EINTR;
        xscore_thread_yield();
    }
    uint64_t count = length < queue->count ? length : queue->count;
    uint8_t *destination = buffer;
    for (uint64_t index = 0; index < count; ++index) {
        destination[index] = queue->data[queue->read_pos];
        queue->read_pos = (queue->read_pos + 1) % XSCORE_PTY_BUF_SIZE;
    }
    queue->count -= (uint32_t)count;
    return (int64_t)count;
}

static int64_t pty_buffer_write(struct xscore_pty_buffer *queue,
                                uint32_t peer_descriptions,
                                const void *buffer, uint64_t length) {
    if (peer_descriptions == 0) return -AKAOS_EPIPE;
    uint64_t written = 0;
    const uint8_t *source = buffer;
    while (written < length) {
        if (queue->count == XSCORE_PTY_BUF_SIZE) {
            if (xscore_process_signal_pending_current()) {
                return written ? (int64_t)written : -AKAOS_EINTR;
            }
            xscore_thread_yield();
            continue;
        }
        queue->data[queue->write_pos] = source[written++];
        queue->write_pos = (queue->write_pos + 1) % XSCORE_PTY_BUF_SIZE;
        ++queue->count;
    }
    return (int64_t)written;
}

static int64_t pty_master_input(struct xscore_pty *pty, const void *buffer,
                                uint64_t length) {
    const uint8_t *source = buffer;
    for (uint64_t index = 0; index < length; ++index) {
        uint8_t c = source[index];
        if (c == '\r' && (pty->termios.c_iflag & AKAOS_ICRNL)) c = '\n';
        if ((pty->termios.c_lflag & AKAOS_ISIG) &&
            c == pty->termios.c_cc[AKAOS_VINTR]) {
            (void)xscore_process_signal_foreground(AKAOS_SIGINT);
            pty->canonical_length = 0;
            continue;
        }
        if ((pty->termios.c_lflag & AKAOS_ISIG) &&
            c == pty->termios.c_cc[AKAOS_VSUSP]) {
            (void)xscore_process_signal_foreground(AKAOS_SIGTSTP);
            pty->canonical_length = 0;
            continue;
        }
        if ((pty->termios.c_lflag & AKAOS_ICANON) == 0) {
            int64_t result = pty_buffer_write(&pty->to_slave,
                                              pty->slave_descriptions, &c, 1);
            if (result < 0) return index ? (int64_t)index : result;
        } else if (c == pty->termios.c_cc[AKAOS_VERASE] || c == '\b') {
            if (pty->canonical_length > 0) --pty->canonical_length;
            if ((pty->termios.c_lflag & AKAOS_ECHO) &&
                (pty->termios.c_lflag & AKAOS_ECHOE)) {
                static const uint8_t erase[] = {'\b', ' ', '\b'};
                (void)pty_buffer_write(&pty->to_master,
                                       pty->master_descriptions, erase,
                                       sizeof(erase));
            }
            continue;
        } else if (c == pty->termios.c_cc[AKAOS_VEOF]) {
            if (pty->canonical_length) {
                int64_t result = pty_buffer_write(
                    &pty->to_slave, pty->slave_descriptions, pty->canonical,
                    pty->canonical_length);
                if (result < 0) return index ? (int64_t)index : result;
                pty->canonical_length = 0;
            }
            continue;
        } else {
            if (pty->canonical_length < sizeof(pty->canonical)) {
                pty->canonical[pty->canonical_length++] = c;
            }
            if (c == '\n') {
                int64_t result = pty_buffer_write(
                    &pty->to_slave, pty->slave_descriptions, pty->canonical,
                    pty->canonical_length);
                if (result < 0) return index ? (int64_t)index : result;
                pty->canonical_length = 0;
            }
        }
        if (pty->termios.c_lflag & AKAOS_ECHO) {
            (void)pty_buffer_write(&pty->to_master,
                                   pty->master_descriptions, &c, 1);
        }
    }
    return (int64_t)length;
}

void xscore_fd_table_initialize(struct xscore_fd_table *table) {
    if (!table) return;
    xscore_spinlock_init(&table->lock);
    fd_table_storage_reset(table);
    uintptr_t pool_flags = file_pool_acquire();
    uintptr_t table_flags = fd_table_acquire(table);
    table->entries[0] = &console_input;
    table->entries[1] = &console_output;
    table->entries[2] = &console_error;
    file_acquire(table->entries[0]);
    file_acquire(table->entries[1]);
    file_acquire(table->entries[2]);
    fd_table_release(table, table_flags);
    file_pool_release(pool_flags);
}

void xscore_fd_table_close_all(struct xscore_fd_table *table) {
    if (!table) return;
    uintptr_t pool_flags = file_pool_acquire();
    uintptr_t table_flags = fd_table_acquire(table);
    for (uint32_t index = 0; index < table->capacity; ++index) {
        file_release(table->entries[index]);
        table->entries[index] = 0;
        table->descriptor_flags[index] = 0;
    }
    struct xscore_file **old_entries =
        table->entries == table->inline_entries ? NULL : table->entries;
    uint32_t *old_flags = table->descriptor_flags ==
                                  table->inline_descriptor_flags
                              ? NULL : table->descriptor_flags;
    fd_table_storage_reset(table);
    fd_table_release(table, table_flags);
    file_pool_release(pool_flags);
    if (old_entries) (void)xscore_heap_free(old_entries);
    if (old_flags) (void)xscore_heap_free(old_flags);
}

int64_t xscore_fd_table_close_range(struct xscore_fd_table *table,
                                    int first_descriptor) {
    if (!table || first_descriptor < 0 || first_descriptor > XSCORE_FD_MAX)
        return -AKAOS_EINVAL;
    uintptr_t pool_flags = file_pool_acquire();
    uintptr_t table_flags = fd_table_acquire(table);
    for (int descriptor = first_descriptor;
         descriptor < (int)table->capacity;
         ++descriptor) {
        file_release(table->entries[descriptor]);
        table->entries[descriptor] = 0;
        table->descriptor_flags[descriptor] = 0;
    }
    fd_table_release(table, table_flags);
    file_pool_release(pool_flags);
    return 0;
}

int xscore_fd_table_has(const struct xscore_fd_table *table, int descriptor) {
    if (!table || descriptor < 0 || descriptor >= XSCORE_FD_MAX) return 0;
    uintptr_t table_flags = fd_table_acquire((struct xscore_fd_table *)table);
    int present = (uint32_t)descriptor < table->capacity &&
                  table->entries[descriptor] != 0;
    fd_table_release((struct xscore_fd_table *)table, table_flags);
    return present;
}

static int64_t fd_read_file(struct xscore_file *file, void *buffer,
                            uint64_t length) {
    if (!file || (file->flags & FILE_READABLE) == 0) return -AKAOS_EBADF;
    if (length == 0) return 0;
    if (file->flags & FILE_DIRECTORY) return -AKAOS_EISDIR;
    if (file->flags & FILE_SIGNALFD) {
        if (length < sizeof(struct signalfd_siginfo)) return -AKAOS_EINVAL;
        uint32_t signal_number = 0;
        uint64_t sender_pid = 0;
        uint32_t sender_uid = 0;
        while (!xscore_process_signalfd_take(file->signal_mask,
                                             &signal_number, &sender_pid,
                                             &sender_uid)) {
            if (file->flags & FILE_NONBLOCK) return -AKAOS_EAGAIN;
            if (xscore_process_signal_pending_current()) return -AKAOS_EINTR;
            xscore_thread_yield();
        }
        struct signalfd_siginfo info = {0};
        info.ssi_signo = signal_number;
        info.ssi_pid = (uint32_t)sender_pid;
        info.ssi_uid = sender_uid;
        info.ssi_code = 0; /* SI_USER */
        for (size_t index = 0; index < sizeof(info); ++index) {
            ((uint8_t *)buffer)[index] = ((const uint8_t *)&info)[index];
        }
        return (int64_t)sizeof(info);
    }
    if (file->flags & FILE_MEMORY) {
        uint64_t remaining = file->size - file->offset;
        if (length > remaining) length = remaining;
        uint8_t *destination = buffer;
        for (uint64_t index = 0; index < length; ++index) {
            destination[index] = file->data[file->offset + index];
        }
        file->offset += length;
        return (int64_t)length;
    }
    if (file->flags & FILE_SHM) {
        int64_t result = xscore_process_shm_read(
            file->node_id, file->offset, buffer, length);
        if (result > 0) file->offset += (uint64_t)result;
        return result;
    }
    if (file->flags & FILE_VNODE) {
        uint64_t bytes_read = 0;
        if (!xscore_vfs_read_node(file->node_id, file->offset, buffer, length,
                                  &bytes_read)) {
            return -AKAOS_EBADF;
        }
        file->offset += bytes_read;
        return (int64_t)bytes_read;
    }
    if (file->flags & FILE_CONSOLE) {
        return xscore_tty_read(buffer, length);
    }
    if (file->flags & FILE_NULL) return 0;
    if (file->flags & FILE_ZERO) {
        uint8_t *destination = buffer;
        for (uint64_t index = 0; index < length; ++index) {
            destination[index] = 0;
        }
        return (int64_t)length;
    }
    if (file->flags & FILE_RANDOM)
        return xscore_random_read(buffer, (size_t)length);
    if (file->flags & FILE_PTY_MASTER) {
        return pty_buffer_read(&file->pty->to_master,
                               file->pty->slave_descriptions, buffer, length);
    }
    if (file->flags & FILE_PTY_SLAVE) {
        return pty_buffer_read(&file->pty->to_slave,
                               file->pty->master_descriptions, buffer, length);
    }
    if (file->flags & FILE_AUDIO) {
        return xscore_audio_read(buffer, length);
    }
    if (file->flags & FILE_NET) {
        return xscore_net_receive(buffer, length);
    }
    if (file->flags & FILE_SOCKET) {
        /* A socket without data does not fail, it waits.  The socket layer
         * answers with EAGAIN when nothing has arrived yet, and a descriptor
         * that is not in non blocking mode waits for the data instead of
         * handing the failure to the program. */
        int64_t result;
        for (;;) {
            result = xscore_socket_receive((int)file->node_id, buffer, length);
            if (result != -AKAOS_EAGAIN) break;
            if (file->flags & FILE_NONBLOCK) break;
            if (xscore_process_signal_pending_current()) return -AKAOS_EINTR;
            xscore_thread_yield();
        }
        return result;
    }
    if (file->flags & FILE_INPUT) {
        return xscore_input_read(&file->offset, buffer, length);
    }
    if (file->flags & FILE_FRAMEBUFFER) {
        if (file->offset != 0) return 0;
        if (length < sizeof(struct akaos_display_info)) {
            return -AKAOS_EINVAL;
        }
        struct akaos_display_info info;
        if (!xscore_framebuffer_get_info(&info)) return -AKAOS_ENODEV;
        *(struct akaos_display_info *)buffer = info;
        file->offset = sizeof(info);
        return (int64_t)sizeof(info);
    }
    if (file->flags & FILE_PIPE) {
        struct xscore_pipe *pipe = file->pipe;
        if (!pipe) return -AKAOS_EBADF;
        while (pipe->count == 0) {
            if (pipe->writers == 0) {
                return 0; /* EOF */
            }
            if (file->flags & FILE_NONBLOCK) return -AKAOS_EAGAIN;
            if (xscore_process_signal_pending_current()) {
                return -AKAOS_EINTR;
            }
            xscore_thread_yield();
        }
        uint64_t to_read = length < (uint64_t)pipe->count ? length : (uint64_t)pipe->count;
        uint8_t *dest = (uint8_t *)buffer;
        for (uint64_t i = 0; i < to_read; ++i) {
            dest[i] = pipe->buffer[pipe->read_pos];
            pipe->read_pos = (pipe->read_pos + 1) % XSCORE_PIPE_BUF_SIZE;
        }
        pipe->count -= (uint32_t)to_read;
        return (int64_t)to_read;
    }
    (void)buffer;
    return -AKAOS_EAGAIN;
}

int64_t xscore_fd_read(struct xscore_fd_table *table, int descriptor,
                       void *buffer, uint64_t length) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return -AKAOS_EBADF;
    uintptr_t operation_flags = file_operation_acquire(file);
    int64_t result = fd_read_file(file, buffer, length);
    file_operation_release(file, operation_flags);
    file_put(file);
    return result;
}

static xscore_console_output_fn g_console_sink = 0;

void xscore_file_set_console_sink(xscore_console_output_fn sink) {
    g_console_sink = sink;
    /* First userspace-facing call site: descriptor/file/pipe pools are in
     * active use from here on, so enable pool locking. */
    file_pool_locked = 1;
}

void xscore_file_enable_lifetime_locking(void) {
    file_pool_locked = 1;
}

xscore_console_output_fn xscore_file_get_console_sink(void) {
    return g_console_sink;
}

static int64_t fd_write_file(struct xscore_file *file, int descriptor,
                             const void *buffer, uint64_t length) {
    if (!file || (file->flags & FILE_WRITABLE) == 0) return -AKAOS_EBADF;
    if (file->flags & FILE_SHM) {
        int64_t result = xscore_process_shm_write(
            file->node_id, file->offset, buffer, length);
        if (result > 0) file->offset += (uint64_t)result;
        return result;
    }
    if (file->flags & FILE_VNODE) {
        if (file->flags & FILE_APPEND) {
            struct akaos_stat st;
            if (xscore_vfs_stat_node_id(file->node_id, &st) == 0) {
                file->offset = st.size;
            }
        }
        uint64_t bytes_written = 0;
        if (!xscore_vfs_write_node(file->node_id, file->offset, buffer, length,
                                   &bytes_written)) {
            return -AKAOS_EROFS;
        }
        file->offset += bytes_written;
        return (int64_t)bytes_written;
    }
    if (file->flags & FILE_PIPE) {
        struct xscore_pipe *pipe = file->pipe;
        if (!pipe) return -AKAOS_EBADF;
        if (pipe->readers == 0) {
            return -AKAOS_EPIPE;
        }
        uint64_t written = 0;
        const uint8_t *src = (const uint8_t *)buffer;
        while (written < length) {
            if (pipe->readers == 0) {
                if (written > 0) return (int64_t)written;
                return -AKAOS_EPIPE;
            }
            if (pipe->count >= XSCORE_PIPE_BUF_SIZE) {
                if (file->flags & FILE_NONBLOCK)
                    return written > 0 ? (int64_t)written : -AKAOS_EAGAIN;
                if (xscore_process_signal_pending_current()) {
                    return written > 0 ? (int64_t)written : -AKAOS_EINTR;
                }
                xscore_thread_yield();
                continue;
            }
            uint32_t space = XSCORE_PIPE_BUF_SIZE - pipe->count;
            uint64_t chunk = (length - written) < (uint64_t)space ? (length - written) : (uint64_t)space;
            for (uint64_t i = 0; i < chunk; ++i) {
                pipe->buffer[pipe->write_pos] = src[written + i];
                pipe->write_pos = (pipe->write_pos + 1) % XSCORE_PIPE_BUF_SIZE;
            }
            pipe->count += (uint32_t)chunk;
            written += chunk;
        }
        return (int64_t)written;
    }
    if (file->flags & FILE_FRAMEBUFFER) {
        int64_t result = xscore_framebuffer_write(file->offset, buffer,
                                                   length);
        if (result < 0) return -AKAOS_ENODEV;
        file->offset += (uint64_t)result;
        return result;
    }
    if (file->flags & FILE_AUDIO) {
        return xscore_audio_write(buffer, length);
    }
    if (file->flags & FILE_NET) {
        return xscore_net_transmit(buffer, length);
    }
    if (file->flags & FILE_SOCKET) {
        /* A socket whose peer has no room does not fail, it waits. */
        int64_t result;
        for (;;) {
            result = xscore_socket_send((int)file->node_id, buffer, length);
            if (result != -AKAOS_EAGAIN) break;
            if (file->flags & FILE_NONBLOCK) break;
            if (xscore_process_signal_pending_current()) return -AKAOS_EINTR;
            xscore_thread_yield();
        }
        return result;
    }
    if (file->flags & (FILE_NULL | FILE_ZERO)) return (int64_t)length;
    if (file->flags & FILE_PTY_MASTER) {
        return pty_master_input(file->pty, buffer, length);
    }
    if (file->flags & FILE_PTY_SLAVE) {
        const uint8_t *source = buffer;
        uint64_t written = 0;
        while (written < length) {
            if (source[written] == '\n' &&
                (file->pty->termios.c_oflag & AKAOS_OPOST) &&
                (file->pty->termios.c_oflag & AKAOS_ONLCR)) {
                static const uint8_t crlf[] = {'\r', '\n'};
                int64_t result = pty_buffer_write(
                    &file->pty->to_master, file->pty->master_descriptions,
                    crlf, sizeof(crlf));
                if (result < 0) return written ? (int64_t)written : result;
            } else {
                int64_t result = pty_buffer_write(
                    &file->pty->to_master, file->pty->master_descriptions,
                    &source[written], 1);
                if (result < 0) return written ? (int64_t)written : result;
            }
            ++written;
        }
        return (int64_t)written;
    }
    const char *text = buffer;
    if (descriptor == 1 || (file->flags & FILE_CONSOLE)) {
        xscore_tty_record_output(text, (size_t)length);
    }
    /* Send one write transaction to the serial console.  Per-character
     * serial calls let a timer IRQ's diagnostic line split a userspace
     * prompt in half (for example `root[INFO]...@akaOS`). */
    xscore_serial_write_n(text, length);
    for (uint64_t index = 0; index < length; ++index) {
        if (g_console_sink &&
            (descriptor == 1 || descriptor == 2 ||
             (file->flags & FILE_CONSOLE))) {
            g_console_sink(text[index]);
        }
    }
    return (int64_t)length;
}

int64_t xscore_fd_write(struct xscore_fd_table *table, int descriptor,
                        const void *buffer, uint64_t length) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return -AKAOS_EBADF;
    uintptr_t operation_flags = file_operation_acquire(file);
    int64_t result = fd_write_file(file, descriptor, buffer, length);
    file_operation_release(file, operation_flags);
    file_put(file);
    return result;
}

int64_t xscore_fd_seek(struct xscore_fd_table *table, int descriptor,
                       int64_t offset, int whence) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return -AKAOS_EBADF;
    uintptr_t operation_flags = file_operation_acquire(file);
    if (file->flags & (FILE_PIPE | FILE_CONSOLE | FILE_PTY_MASTER |
                       FILE_PTY_SLAVE | FILE_INPUT | FILE_AUDIO | FILE_NET |
                       FILE_SOCKET)) {
        file_operation_release(file, operation_flags);
        file_put(file);
        return -AKAOS_EINVAL;
    }

    uint64_t end = file->size;
    if (file->flags & FILE_VNODE) {
        struct akaos_stat stat;
        if (xscore_vfs_stat_node_id(file->node_id, &stat) == 0) end = stat.size;
    } else if (file->flags & FILE_FRAMEBUFFER) {
        struct akaos_display_info info;
        if (!xscore_framebuffer_get_info(&info)) {
            file_operation_release(file, operation_flags);
            file_put(file);
            return -AKAOS_ENODEV;
        }
        end = info.byte_size;
    } else if (file->flags & FILE_SHM) {
        int64_t size = xscore_process_shm_size(file->node_id);
        if (size < 0) {
            file_operation_release(file, operation_flags);
            file_put(file);
            return size;
        }
        end = (uint64_t)size;
    }

    int64_t base = 0;
    if (whence == AKAOS_SEEK_CUR) {
        if (file->offset > INT64_MAX) base = INT64_MAX;
        else base = (int64_t)file->offset;
    } else if (whence == AKAOS_SEEK_END) {
        if (end > INT64_MAX) base = INT64_MAX;
        else base = (int64_t)end;
    } else if (whence != AKAOS_SEEK_SET) {
        file_operation_release(file, operation_flags);
        file_put(file);
        return -AKAOS_EINVAL;
    }
    if ((offset > 0 && base > INT64_MAX - offset) ||
        (offset < 0 && base < INT64_MIN - offset)) {
        file_operation_release(file, operation_flags);
        file_put(file);
        return -AKAOS_EINVAL;
    }
    int64_t next = base + offset;
    if (next < 0 || ((file->flags & FILE_FRAMEBUFFER) &&
                     (uint64_t)next > end)) {
        file_operation_release(file, operation_flags);
        file_put(file);
        return -AKAOS_EINVAL;
    }
    file->offset = (uint64_t)next;
    file_operation_release(file, operation_flags);
    file_put(file);
    return next;
}

static int16_t fd_poll_file(struct xscore_file *file, int16_t events) {
    if (!file) return AKAOS_POLLNVAL;

    int16_t ready = 0;
    if (file->flags & FILE_SIGNALFD) {
        if (xscore_process_signalfd_pending(file->signal_mask)) {
            ready |= AKAOS_POLLIN;
        }
    } else if (file->flags & FILE_CONSOLE) {
        xscore_tty_poll_keyboard();
        if ((file->flags & FILE_READABLE) && xscore_tty_has_line()) {
            ready |= AKAOS_POLLIN;
        }
        if (file->flags & FILE_WRITABLE) ready |= AKAOS_POLLOUT;
    } else if (file->flags & (FILE_NULL | FILE_ZERO)) {
        if (file->flags & FILE_READABLE) ready |= AKAOS_POLLIN;
        if (file->flags & FILE_WRITABLE) ready |= AKAOS_POLLOUT;
    } else if (file->flags & FILE_AUDIO) {
        if (file->flags & FILE_READABLE) ready |= AKAOS_POLLIN;
        if ((file->flags & FILE_WRITABLE) && xscore_audio_poll_write_ready()) {
            ready |= AKAOS_POLLOUT;
        }
    } else if (file->flags & FILE_NET) {
        if ((file->flags & FILE_READABLE) && xscore_net_poll_readable()) {
            ready |= AKAOS_POLLIN;
        }
        if ((file->flags & FILE_WRITABLE) && xscore_net_poll_writable()) {
            ready |= AKAOS_POLLOUT;
        }
    } else if (file->flags & FILE_SOCKET) {
        if ((file->flags & FILE_READABLE) && xscore_socket_poll_readable((int)file->node_id)) {
            ready |= AKAOS_POLLIN;
        }
        if ((file->flags & FILE_WRITABLE) && xscore_socket_poll_writable((int)file->node_id)) {
            ready |= AKAOS_POLLOUT;
        }
    } else if (file->flags & FILE_INPUT) {
        if (xscore_input_ready(file->offset)) ready |= AKAOS_POLLIN;
    } else if (file->flags & FILE_FRAMEBUFFER) {
        if ((file->flags & FILE_READABLE) && file->offset == 0) {
            ready |= AKAOS_POLLIN;
        }
        if ((file->flags & FILE_WRITABLE) && file->offset < file->size) {
            ready |= AKAOS_POLLOUT;
        }
    } else if (file->flags & FILE_PIPE) {
        struct xscore_pipe *pipe = file->pipe;
        if (!pipe) return AKAOS_POLLNVAL;
        if (file->flags & FILE_READABLE) {
            if (pipe->count != 0) ready |= AKAOS_POLLIN;
            if (pipe->writers == 0) ready |= AKAOS_POLLHUP;
        }
        if (file->flags & FILE_WRITABLE) {
            if (pipe->readers == 0) {
                ready |= AKAOS_POLLERR;
            } else if (pipe->count < XSCORE_PIPE_BUF_SIZE) {
                ready |= AKAOS_POLLOUT;
            }
        }
    } else if (file->flags & (FILE_PTY_MASTER | FILE_PTY_SLAVE)) {
        struct xscore_pty_buffer *input =
            (file->flags & FILE_PTY_MASTER) ? &file->pty->to_master
                                            : &file->pty->to_slave;
        uint32_t peer = (file->flags & FILE_PTY_MASTER)
                            ? file->pty->slave_descriptions
                            : file->pty->master_descriptions;
        if ((file->flags & FILE_READABLE) && input->count) ready |= AKAOS_POLLIN;
        if ((file->flags & FILE_READABLE) && peer == 0) ready |= AKAOS_POLLHUP;
        if ((file->flags & FILE_WRITABLE) && peer != 0) ready |= AKAOS_POLLOUT;
        if ((file->flags & FILE_WRITABLE) && peer == 0) ready |= AKAOS_POLLERR;
    } else {
        /* Regular files, shared-memory files, and memory files do not block
         * in the descriptor layer. */
        if (file->flags & FILE_READABLE) ready |= AKAOS_POLLIN;
        if (file->flags & FILE_WRITABLE) ready |= AKAOS_POLLOUT;
    }

    return (int16_t)((ready & events) |
                     (ready & (AKAOS_POLLERR | AKAOS_POLLHUP |
                               AKAOS_POLLNVAL)));
}

int16_t xscore_fd_poll(struct xscore_fd_table *table, int descriptor,
                       int16_t events) {
    struct xscore_file *file = fd_lookup_ref(table, descriptor);
    if (!file) return AKAOS_POLLNVAL;
    uintptr_t operation_flags = file_operation_acquire(file);
    int16_t result = fd_poll_file(file, events);
    file_operation_release(file, operation_flags);
    file_put(file);
    return result;
}

int64_t xscore_fd_close(struct xscore_fd_table *table, int descriptor) {
    if (!table || descriptor < 0 || descriptor >= XSCORE_FD_MAX) {
        return -AKAOS_EBADF;
    }
    uintptr_t pool_flags = file_pool_acquire();
    uintptr_t table_flags = fd_table_acquire(table);
    struct xscore_file *file = (uint32_t)descriptor < table->capacity
                                    ? table->entries[descriptor]
                                    : 0;
    if (!file) {
        fd_table_release(table, table_flags);
        file_pool_release(pool_flags);
        return -AKAOS_EBADF;
    }
    table->entries[descriptor] = 0;
    table->descriptor_flags[descriptor] = 0;
    file_release(file);
    fd_table_release(table, table_flags);
    file_pool_release(pool_flags);
    return 0;
}

int64_t xscore_fd_dup(struct xscore_fd_table *table, int descriptor) {
    if (!table || descriptor < 0 || descriptor >= XSCORE_FD_MAX) {
        return -AKAOS_EBADF;
    }
    uintptr_t pool_flags = file_pool_acquire();
    uintptr_t table_flags = fd_table_acquire(table);
    struct xscore_file *file = (uint32_t)descriptor < table->capacity
                                    ? table->entries[descriptor]
                                    : 0;
    if (!file) {
        fd_table_release(table, table_flags);
        file_pool_release(pool_flags);
        return -AKAOS_EBADF;
    }
    int result = -AKAOS_EMFILE;
    for (;;) {
        for (int target = 0; target < (int)table->capacity; ++target) {
            if (!table->entries[target]) {
                table->entries[target] = file;
                table->descriptor_flags[target] = 0;
                file_acquire(file);
                result = target;
                break;
            }
        }
        if (result >= 0 || table->capacity >= XSCORE_FD_MAX) break;
        uint32_t next_capacity = table->capacity > XSCORE_FD_MAX / 2U
                                     ? XSCORE_FD_MAX
                                     : table->capacity * 2U;
        result = fd_table_grow_locked(table, next_capacity);
        if (result != 0) break;
        result = -AKAOS_EMFILE;
    }
    fd_table_release(table, table_flags);
    file_pool_release(pool_flags);
    return result;
}

int64_t xscore_fd_dup2(struct xscore_fd_table *table, int old_descriptor,
                       int new_descriptor) {
    if (!table || old_descriptor < 0 || old_descriptor >= XSCORE_FD_MAX ||
        new_descriptor < 0 || new_descriptor >= XSCORE_FD_MAX) {
        return -AKAOS_EBADF;
    }
    uintptr_t pool_flags = file_pool_acquire();
    uintptr_t table_flags = fd_table_acquire(table);
    int grow_error = 0;
    if ((uint32_t)new_descriptor >= table->capacity)
        grow_error = fd_table_grow_locked(table, (uint32_t)new_descriptor + 1U);
    struct xscore_file *file = (uint32_t)old_descriptor < table->capacity
                                   ? table->entries[old_descriptor]
                                   : 0;
    if (grow_error != 0) {
        fd_table_release(table, table_flags);
        file_pool_release(pool_flags);
        return grow_error;
    }
    if (!file || new_descriptor < 0 || new_descriptor >= XSCORE_FD_MAX) {
        fd_table_release(table, table_flags);
        file_pool_release(pool_flags);
        return -AKAOS_EBADF;
    }
    if (old_descriptor == new_descriptor) {
        fd_table_release(table, table_flags);
        file_pool_release(pool_flags);
        return new_descriptor;
    }
    if (table->entries[new_descriptor]) {
        file_release(table->entries[new_descriptor]);
    }
    table->entries[new_descriptor] = file;
    table->descriptor_flags[new_descriptor] = 0;
    file_acquire(file);
    fd_table_release(table, table_flags);
    file_pool_release(pool_flags);
    return new_descriptor;
}

int64_t xscore_fd_fcntl(struct xscore_fd_table *table, int descriptor,
                        int command, uint64_t argument) {
    if (!table || descriptor < 0 || descriptor >= XSCORE_FD_MAX)
        return -AKAOS_EBADF;

    if (command == AKAOS_F_GETFD || command == AKAOS_F_SETFD) {
        uintptr_t table_flags = fd_table_acquire(table);
        if ((uint32_t)descriptor >= table->capacity ||
            !table->entries[descriptor]) {
            fd_table_release(table, table_flags);
            return -AKAOS_EBADF;
        }
        if (command == AKAOS_F_GETFD) {
            int64_t result = table->descriptor_flags[descriptor];
            fd_table_release(table, table_flags);
            return result;
        }
        if ((argument & ~((uint64_t)AKAOS_FD_CLOEXEC)) != 0U) {
            fd_table_release(table, table_flags);
            return -AKAOS_EINVAL;
        }
        table->descriptor_flags[descriptor] = (uint32_t)argument;
        fd_table_release(table, table_flags);
        return 0;
    }

    if (command == AKAOS_F_DUPFD || command == AKAOS_F_DUPFD_CLOEXEC) {
        if (argument >= XSCORE_FD_MAX) return -AKAOS_EINVAL;
        uintptr_t pool_flags = file_pool_acquire();
        uintptr_t table_flags = fd_table_acquire(table);
        struct xscore_file *file = (uint32_t)descriptor < table->capacity
                                       ? table->entries[descriptor]
                                       : 0;
        if (!file) {
            fd_table_release(table, table_flags);
            file_pool_release(pool_flags);
            return -AKAOS_EBADF;
        }
        int result = fd_table_grow_locked(table, (uint32_t)argument + 1U);
        if (result == 0) {
            result = -AKAOS_EMFILE;
            for (;;) {
                for (int target = (int)argument;
                     target < (int)table->capacity; ++target) {
                    if (table->entries[target]) continue;
                    table->entries[target] = file;
                    table->descriptor_flags[target] =
                        command == AKAOS_F_DUPFD_CLOEXEC
                            ? AKAOS_FD_CLOEXEC : 0U;
                    file_acquire(file);
                    result = target;
                    break;
                }
                if (result >= 0 || table->capacity >= XSCORE_FD_MAX) break;
                uint32_t next_capacity = table->capacity > XSCORE_FD_MAX / 2U
                                             ? XSCORE_FD_MAX
                                             : table->capacity * 2U;
                int grow_result = fd_table_grow_locked(table, next_capacity);
                if (grow_result != 0) {
                    result = grow_result;
                    break;
                }
            }
        }
        fd_table_release(table, table_flags);
        file_pool_release(pool_flags);
        return result;
    }

    if (command == AKAOS_F_GETFL || command == AKAOS_F_SETFL) {
        struct xscore_file *file = fd_lookup_ref(table, descriptor);
        if (!file) return -AKAOS_EBADF;
        uintptr_t operation_flags = file_operation_acquire(file);
        int64_t result = 0;
        if (command == AKAOS_F_GETFL) {
            result = (file->flags & FILE_WRITABLE)
                ? ((file->flags & FILE_READABLE) ? AKAOS_O_RDWR
                                                 : AKAOS_O_WRONLY)
                : AKAOS_O_RDONLY;
            if (file->flags & FILE_APPEND) result |= AKAOS_O_APPEND;
            if (file->flags & FILE_NONBLOCK) result |= AKAOS_O_NONBLOCK;
        } else {
            uint32_t mutable_flags = FILE_APPEND | FILE_NONBLOCK;
            file->flags &= ~mutable_flags;
            if (argument & AKAOS_O_APPEND) file->flags |= FILE_APPEND;
            if (argument & AKAOS_O_NONBLOCK) file->flags |= FILE_NONBLOCK;
        }
        file_operation_release(file, operation_flags);
        file_put(file);
        return result;
    }

    if (command == AKAOS_F_ADD_SEALS || command == AKAOS_F_GET_SEALS)
        return xscore_process_shm_fcntl(descriptor, command, argument);

    return -AKAOS_EINVAL;
}

void xscore_fd_table_close_cloexec(struct xscore_fd_table *table) {
    if (!table) return;
    uintptr_t pool_flags = file_pool_acquire();
    uintptr_t table_flags = fd_table_acquire(table);
    for (uint32_t index = 0; index < table->capacity; ++index) {
        if (table->entries[index] &&
            (table->descriptor_flags[index] & AKAOS_FD_CLOEXEC)) {
            file_release(table->entries[index]);
            table->entries[index] = 0;
            table->descriptor_flags[index] = 0;
        }
    }
    fd_table_release(table, table_flags);
    file_pool_release(pool_flags);
}

int xscore_fd_table_clone(struct xscore_fd_table *dest,
                          const struct xscore_fd_table *src) {
    if (!dest) return -AKAOS_EINVAL;
    if (src == dest) return 0;
    xscore_spinlock_init(&dest->lock);
    fd_table_storage_reset(dest);
    if (!src) return 0;
    uintptr_t pool_flags = file_pool_acquire();
    uintptr_t src_flags = fd_table_acquire((struct xscore_fd_table *)src);
    uintptr_t dest_flags = fd_table_acquire(dest);
    int error = fd_table_grow_locked(dest, src->capacity);
    if (error == 0) {
        for (uint32_t index = 0; index < src->capacity; ++index) {
            dest->entries[index] = src->entries[index];
            dest->descriptor_flags[index] = src->descriptor_flags[index];
            if (!dest->entries[index]) continue;
            file_acquire(dest->entries[index]);
        }
    }
    fd_table_release(dest, dest_flags);
    fd_table_release((struct xscore_fd_table *)src, src_flags);
    file_pool_release(pool_flags);
    if (error != 0) xscore_fd_table_close_all(dest);
    return error;
}

int xscore_pipe_create(struct xscore_fd_table *table, int *out_read_fd,
                       int *out_write_fd, uint32_t flags) {
    if (!table || !out_read_fd || !out_write_fd) return -AKAOS_EBADF;
    if ((flags & ~(AKAOS_O_CLOEXEC | AKAOS_O_NONBLOCK)) != 0U)
        return -AKAOS_EINVAL;
    uintptr_t lock_flags = 0;
    if (file_pool_locked) lock_flags = xscore_spin_lock_irqsave(&file_pool_lock);

    struct xscore_pipe *pipe = 0;
    for (uint32_t i = 0; i < XSCORE_PIPE_POOL_SIZE; ++i) {
        if (!pipe_pool[i].in_use) {
            pipe = &pipe_pool[i];
            break;
        }
    }
    if (!pipe) {
        if (file_pool_locked) xscore_spin_unlock_irqrestore(&file_pool_lock, lock_flags);
        xscore_serial_write("[ERROR] pipe creation: kernel pipe pool exhausted\n");
        return -AKAOS_ENFILE;
    }

    struct xscore_file *read_file = 0;
    struct xscore_file *write_file = 0;
    for (uint32_t i = 0; i < XSCORE_FILE_POOL_SIZE; ++i) {
        if (file_pool[i].references == 0 && file_pool[i].flags == 0) {
            if (!read_file) {
                read_file = &file_pool[i];
            } else if (!write_file) {
                write_file = &file_pool[i];
                break;
            }
        }
    }
    if (!read_file || !write_file) {
        if (file_pool_locked) xscore_spin_unlock_irqrestore(&file_pool_lock, lock_flags);
        xscore_serial_write("[ERROR] pipe creation: file description pool exhausted\n");
        return -AKAOS_ENFILE;
    }

    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->count = 0;
    pipe->readers = 1;
    pipe->writers = 1;
    pipe->in_use = 1;

    uint32_t file_flags = (flags & AKAOS_O_NONBLOCK) ? FILE_NONBLOCK : 0U;
    uint32_t descriptor_flags =
        (flags & AKAOS_O_CLOEXEC) ? AKAOS_FD_CLOEXEC : 0U;
    read_file->flags = FILE_READABLE | FILE_PIPE | file_flags;
    read_file->references = 0;
    read_file->offset = 0;
    read_file->data = 0;
    read_file->size = 0;
    read_file->node_id = 0;
    read_file->pipe = pipe;

    write_file->flags = FILE_WRITABLE | FILE_PIPE | file_flags;
    write_file->references = 0;
    write_file->offset = 0;
    write_file->data = 0;
    write_file->size = 0;
    write_file->node_id = 0;
    write_file->pipe = pipe;

    int rfd = fd_install_with_flags(table, read_file, descriptor_flags);
    if (rfd < 0) {
        pipe->in_use = 0;
        read_file->flags = 0;
        write_file->flags = 0;
        if (file_pool_locked) xscore_spin_unlock_irqrestore(&file_pool_lock, lock_flags);
        xscore_serial_write("[ERROR] pipe creation: read descriptor allocation failed\n");
        return rfd;
    }
    int wfd = fd_install_with_flags(table, write_file, descriptor_flags);
    if (wfd < 0) {
        table->entries[rfd] = 0;
        table->descriptor_flags[rfd] = 0;
        file_release(read_file);
        pipe->in_use = 0;
        write_file->flags = 0;
        if (file_pool_locked) xscore_spin_unlock_irqrestore(&file_pool_lock, lock_flags);
        xscore_serial_write("[ERROR] pipe creation: write descriptor allocation failed\n");
        return wfd;
    }

    *out_read_fd = rfd;
    *out_write_fd = wfd;
    if (file_pool_locked) xscore_spin_unlock_irqrestore(&file_pool_lock, lock_flags);
    return 0;
}
