#ifndef XSCORE_VFS_H
#define XSCORE_VFS_H

#include <stddef.h>
#include <stdint.h>
#include <akaos/fs.h>

struct xscore_fd_table;

#if defined(XSCORE_LOW_MEMORY)
#define XSCORE_VFS_MAX_NODES 512
#else
#define XSCORE_VFS_MAX_NODES 2048
#endif

enum xscore_device_kind {
    XSCORE_DEVICE_NONE = 0,
    XSCORE_DEVICE_TTY1,
    XSCORE_DEVICE_NULL,
    XSCORE_DEVICE_ZERO,
    XSCORE_DEVICE_RANDOM,
    XSCORE_DEVICE_FRAMEBUFFER,
    XSCORE_DEVICE_INPUT_EVENT0,
    XSCORE_DEVICE_PTMX,
    XSCORE_DEVICE_PTS,
    XSCORE_DEVICE_AUDIO,
    XSCORE_DEVICE_NET,
    XSCORE_DEVICE_GPU0,
    XSCORE_DEVICE_NVME0N1,
};

enum xscore_proc_kind {
    XSCORE_PROC_NONE = 0,
    XSCORE_PROC_ROOT,
    XSCORE_PROC_SELF_DIR,
    XSCORE_PROC_SELF_STATUS,
    XSCORE_PROC_PROCESSES,
    XSCORE_PROC_MOUNTS,
    XSCORE_PROC_PID_DIR,
    XSCORE_PROC_PID_STATUS,
    XSCORE_PROC_UPTIME,
    XSCORE_PROC_REALTIME,
    XSCORE_PROC_MEMINFO,
    XSCORE_PROC_SLABINFO,
    XSCORE_PROC_NET_DIR,
    XSCORE_PROC_NET_TCP,
    XSCORE_PROC_NET_UDP,
    XSCORE_PROC_NET_ROUTE,
    XSCORE_PROC_NET_ARP,
    XSCORE_PROC_NET_DEV,
    XSCORE_PROC_CPUINFO,
    XSCORE_PROC_GPUINFO,
    XSCORE_PROC_PCI,
    XSCORE_PROC_ASOUND_DIR,
    XSCORE_PROC_ASOUND_CARDS,
    XSCORE_PROC_ACPI_DIR,
    XSCORE_PROC_ACPI_INFO,
    XSCORE_PROC_ACPI_FADT,
    XSCORE_PROC_ACPI_MADT,
    XSCORE_PROC_ACPI_BATTERY,
    XSCORE_PROC_ACPI_THERMAL,
};

#define XSCORE_OPEN_READ_ONLY AKAOS_O_RDONLY
#define XSCORE_OPEN_WRITE_ONLY AKAOS_O_WRONLY
#define XSCORE_OPEN_READ_WRITE AKAOS_O_RDWR
#define XSCORE_OPEN_CREAT AKAOS_O_CREAT
#define XSCORE_OPEN_TRUNC AKAOS_O_TRUNC
#define XSCORE_OPEN_APPEND AKAOS_O_APPEND

int xscore_vfs_initialize(void);
int xscore_vfs_add_directory(const char *path, uint32_t mode);
int xscore_vfs_add_file(const char *path, const uint8_t *data, size_t size,
                        uint32_t mode);
int xscore_vfs_add_character_device(const char *path, uint32_t mode);
int xscore_vfs_install_devfs(void);
/* Internal PTY lifetime/authorization helpers; prepare returns a held vnode. */
int xscore_vfs_prepare_pts(uint32_t number, uint32_t *node_id);
int xscore_vfs_pts_access(uint32_t node_id, uint32_t access);
int xscore_vfs_install_procfs(void);
enum xscore_proc_kind xscore_vfs_proc_kind_at(uint32_t cwd_node,
                                              const char *path);
enum xscore_proc_kind xscore_vfs_proc_kind_pid_at(uint32_t cwd_node,
                                                  const char *path,
                                                  uint64_t *out_pid);
enum xscore_device_kind xscore_vfs_device_kind_at(uint32_t cwd_node,
                                                   const char *path);
int xscore_vfs_device_number_at(uint32_t cwd_node, const char *path,
                                uint32_t *number, uint32_t *node_id);
int xscore_vfs_lookup_file(const char *path, const uint8_t **data,
                           size_t *size);
int xscore_vfs_lookup_file_at(uint32_t cwd_node, const char *path,
                              const uint8_t **data, size_t *size);
int xscore_vfs_lookup_node(uint32_t node, const uint8_t **data,
                           size_t *size);
/* Hold a vnode reference across operations that retain its backing bytes
 * (open file descriptions and executable image reservations). */
int xscore_vfs_node_acquire(uint32_t node);
void xscore_vfs_node_release(uint32_t node);
int xscore_vfs_lookup_file_ref_at(uint32_t cwd_node, const char *path,
                                  const uint8_t **data, size_t *size,
                                  uint32_t *node);

int xscore_vfs_resolve_path_at(uint32_t cwd_node, const char *path,
                               uint32_t *out_node);
int xscore_vfs_build_node_path(uint32_t node_id, char *path, size_t capacity);
int xscore_vfs_stat_at(uint32_t cwd_node, const char *path,
                       struct akaos_stat *status);
int64_t xscore_vfs_open_at(struct xscore_fd_table *table, uint32_t cwd_node,
                           const char *path, uint64_t flags, uint32_t mode);
int xscore_vfs_mkdir_at(uint32_t cwd_node, const char *path, uint32_t mode);
int xscore_vfs_unlink_at(uint32_t cwd_node, const char *path);
int xscore_vfs_rename_at(uint32_t old_cwd, const char *old_path,
                         uint32_t new_cwd, const char *new_path);
int xscore_vfs_chmod_node(uint32_t target, uint32_t mode);
int xscore_vfs_chown_node(uint32_t target, uint32_t uid, uint32_t gid);
int xscore_vfs_chmod_at(uint32_t cwd_node, const char *path, uint32_t mode);
int xscore_vfs_chown_at(uint32_t cwd_node, const char *path, uint32_t uid, uint32_t gid);
int xscore_vfs_access_at(uint32_t cwd_node, const char *path, uint32_t access);
int64_t xscore_vfs_getdents(struct xscore_fd_table *table, int descriptor,
                            struct akaos_dirent *entries, uint64_t capacity);

/* Node buffer access for file descriptions */
int xscore_vfs_read_node(uint32_t node_id, uint64_t offset, void *buffer,
                         uint64_t length, uint64_t *bytes_read);
int xscore_vfs_write_node(uint32_t node_id, uint64_t offset, const void *buffer,
                          uint64_t length, uint64_t *bytes_written);
int64_t xscore_vfs_truncate_node(uint32_t node_id, uint64_t length);
int xscore_vfs_node_is_immutable_regular(uint32_t node_id);

/* Compatibility wrappers */
int64_t xscore_vfs_open(struct xscore_fd_table *table, const char *path,
                        uint64_t flags);
int xscore_vfs_stat(const char *path, struct akaos_stat *status);
int xscore_vfs_stat_node_id(uint32_t node_id, struct akaos_stat *status);
uint32_t xscore_vfs_node_count(void);

/* Mount and synchronize the persistent akaFS overlay on the virtio data disk. */
int xscore_vfs_persistence_mount(void);
int xscore_vfs_persistence_sync(void);

/* Mount table management */
int xscore_vfs_mount(const char *source, const char *target, const char *fstype, uint64_t flags);
int xscore_vfs_umount(const char *target, uint64_t flags);
size_t xscore_vfs_format_mounts(char *buffer, size_t capacity);
size_t xscore_vfs_format_uptime(char *buffer, size_t capacity);
size_t xscore_vfs_format_meminfo(char *buffer, size_t capacity);

/* Tab autocompletion helper */
int xscore_vfs_autocomplete(uint32_t cwd_node, const char *word, int is_command,
                            char *completion, size_t max_completion,
                            int *is_dir, char matches[][32], size_t max_matches,
                            size_t *out_match_count);

#endif
