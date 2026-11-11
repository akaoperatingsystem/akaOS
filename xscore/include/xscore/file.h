#ifndef XSCORE_FILE_H
#define XSCORE_FILE_H

#include <stdint.h>
#include <stddef.h>
#include <xscore/smp.h>

#define XSCORE_FD_MAX 1024
#define XSCORE_FD_INLINE_MAX 16U

struct xscore_file;

struct xscore_fd_table {
    struct xscore_file *inline_entries[XSCORE_FD_INLINE_MAX];
    uint32_t inline_descriptor_flags[XSCORE_FD_INLINE_MAX];
    struct xscore_file **entries;
    uint32_t *descriptor_flags;
    uint32_t capacity;
    /* Descriptor replacement/close and lookup are shared-state operations.
     * Keep the table lock in the public object so every owner (processes and
     * short-lived kernel helper tables) gets the same lifetime protection. */
    xscore_spinlock_t lock;
};

void xscore_fd_table_initialize(struct xscore_fd_table *table);
void xscore_fd_table_close_all(struct xscore_fd_table *table);
int64_t xscore_fd_table_close_range(struct xscore_fd_table *table,
                                    int first_descriptor);
int xscore_fd_table_has(const struct xscore_fd_table *table, int descriptor);
int64_t xscore_fd_open_memory(struct xscore_fd_table *table,
                              const uint8_t *data, uint64_t size);
int64_t xscore_fd_open_generated(struct xscore_fd_table *table,
                                 const uint8_t *data, uint64_t size);
int64_t xscore_fd_open_directory(struct xscore_fd_table *table,
                                 uint32_t node_id);
int64_t xscore_fd_open_vnode(struct xscore_fd_table *table,
                             uint32_t node_id, uint64_t open_flags);
int xscore_fd_metadata(struct xscore_fd_table *table, int descriptor,
                       int ownership, uint32_t first, uint32_t second);
int xscore_fd_directory_state(struct xscore_fd_table *table, int descriptor,
                              uint32_t *node_id, uint64_t *offset);
int xscore_fd_directory_set_offset(struct xscore_fd_table *table,
                                   int descriptor, uint64_t offset);
int xscore_fd_vnode_state(struct xscore_fd_table *table, int descriptor,
                          uint32_t *node_id, int *readable, int *writable);
int64_t xscore_fd_open_shm(struct xscore_fd_table *table, uint32_t object_id,
                           int readable, int writable,
                           uint32_t descriptor_flags);
int64_t xscore_fd_open_input(struct xscore_fd_table *table);
int64_t xscore_fd_open_framebuffer(struct xscore_fd_table *table,
                                   int readable, int writable);
int64_t xscore_fd_open_tty(struct xscore_fd_table *table,
                           int readable, int writable);
int64_t xscore_fd_open_null(struct xscore_fd_table *table,
                            int readable, int writable);
int64_t xscore_fd_open_zero(struct xscore_fd_table *table,
                            int readable, int writable);
int64_t xscore_fd_open_random(struct xscore_fd_table *table);
int64_t xscore_fd_signalfd(struct xscore_fd_table *table, int descriptor,
                           uint32_t signal_mask, uint32_t flags);
int64_t xscore_fd_open_audio(struct xscore_fd_table *table,
                             int readable, int writable);
int64_t xscore_fd_open_net(struct xscore_fd_table *table,
                           int readable, int writable);
int64_t xscore_fd_open_socket(struct xscore_fd_table *table, int sock_id);
int xscore_fd_socket_id(struct xscore_fd_table *table, int descriptor);
int xscore_fd_socket_acquire(struct xscore_fd_table *table, int descriptor,
                             int *sock_id, struct xscore_file **reference);
int xscore_fd_transfer_acquire(struct xscore_fd_table *table, int descriptor,
                               struct xscore_file **transfer);
int64_t xscore_fd_transfer_install_many(
    struct xscore_fd_table *table, struct xscore_file *const *transfers,
    size_t count, int *descriptors);
void xscore_fd_transfer_release(struct xscore_file *transfer);
int xscore_fd_is_tty(struct xscore_fd_table *table, int descriptor);
int64_t xscore_fd_open_ptmx(struct xscore_fd_table *table);
int64_t xscore_fd_open_pts(struct xscore_fd_table *table, uint32_t number,
                           uint32_t slave_node, int readable, int writable);
int xscore_fd_pts_number(struct xscore_fd_table *table, int descriptor,
                         uint32_t *number);
int64_t xscore_fd_ioctl(struct xscore_fd_table *table, int descriptor,
                        uint32_t request, void *argument);
int xscore_fd_shm_state(struct xscore_fd_table *table, int descriptor,
                        uint32_t *object_id, int *readable, int *writable);
int64_t xscore_fd_read(struct xscore_fd_table *table, int descriptor,
                       void *buffer, uint64_t length);
int64_t xscore_fd_write(struct xscore_fd_table *table, int descriptor,
                        const void *buffer, uint64_t length);
int64_t xscore_fd_seek(struct xscore_fd_table *table, int descriptor,
                       int64_t offset, int whence);
int64_t xscore_fd_close(struct xscore_fd_table *table, int descriptor);
int64_t xscore_fd_fcntl(struct xscore_fd_table *table, int descriptor,
                        int command, uint64_t argument);
int64_t xscore_fd_flock(struct xscore_fd_table *table, int descriptor,
                        int operation);
void xscore_fd_table_close_cloexec(struct xscore_fd_table *table);
int64_t xscore_fd_dup(struct xscore_fd_table *table, int descriptor);
int64_t xscore_fd_dup2(struct xscore_fd_table *table, int old_descriptor,
                       int new_descriptor);
int16_t xscore_fd_poll(struct xscore_fd_table *table, int descriptor,
                       int16_t events);
int xscore_fd_table_clone(struct xscore_fd_table *dest,
                          const struct xscore_fd_table *src);
int xscore_pipe_create(struct xscore_fd_table *table, int *out_read_fd,
                       int *out_write_fd, uint32_t flags);
typedef void (*xscore_console_output_fn)(char c);
void xscore_file_set_console_sink(xscore_console_output_fn sink);
xscore_console_output_fn xscore_file_get_console_sink(void);
/* Enable pool/reference locking before any multicore kernel workload. */
void xscore_file_enable_lifetime_locking(void);

#endif
