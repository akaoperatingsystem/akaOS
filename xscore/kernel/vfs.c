#include <xscore/bufcache.h>
#include <xscore/file.h>
#include <xscore/memory.h>
#include <xscore/process.h>
#include <xscore/smp.h>
#include <xscore/serial.h>
#include <xscore/virtio_blk.h>
#include <xscore/vfs.h>
#include <xscore/audio.h>
#include <xscore/net.h>
#include <xscore/socket.h>
#include <xscore/timer.h>
#include <xscore/cpu.h>
#include <xscore/gpu.h>
#include <xscore/pci.h>
#include <xscore/usb.h>
#include <xscore/slab.h>
#include <xscore/sound.h>
#include <xscore/acpi.h>
#include <xscore/power.h>
#include <xscore/time.h>
#include <xscore/usercopy.h>
#include <akaos/errno.h>
#include <stddef.h>
#include <stdint.h>

#define XSCORE_VFS_NAME_MAX AKAOS_NAME_MAX
#define XSCORE_VFS_BLOCK_SIZE 4096
#define XSCORE_VFS_DIRECT_BLOCKS 32
#define XSCORE_VFS_FILE_MAX_SIZE (XSCORE_VFS_DIRECT_BLOCKS * XSCORE_VFS_BLOCK_SIZE) /* 128 KiB */
#define XSCORE_VFS_PATH_MAX 127
#define AKAOS_FS_SUPERBLOCK_A_SECTOR UINT64_C(8)
#define AKAOS_FS_SUPERBLOCK_B_SECTOR UINT64_C(9)
#define AKAOS_FS_PAYLOAD_SECTOR UINT64_C(10)
#define AKAOS_FS_SECTOR_SIZE 512
#define AKAOS_FS_PAYLOAD_MAX (512 * 1024)
#define AKAOS_FS_VERSION UINT32_C(2)

enum xscore_vnode_type {
    XSCORE_VNODE_UNUSED = 0,
    XSCORE_VNODE_DIRECTORY,
    XSCORE_VNODE_REGULAR,
    XSCORE_VNODE_CHARACTER,
};

struct xscore_vnode {
    enum xscore_vnode_type type;
    uint32_t parent;
    char name[XSCORE_VFS_NAME_MAX + 1];
    const uint8_t *data;
    size_t size;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    int is_dynamic;
    uint32_t references;
    int unlinked;
    uint64_t block_pages[XSCORE_VFS_DIRECT_BLOCKS];
};

static struct xscore_vnode nodes[XSCORE_VFS_MAX_NODES];
/* SMP safety: one tree lock covers the vnode array, node_count, persistence
 * flags, and every dynamic inode/data mutation. Never held across blocking
 * I/O — bufcache/disk calls happen outside or with re-entrant polling paths. */
static xscore_spinlock_t vfs_lock = XSCORE_SPINLOCK_INIT;
static int vfs_locked;
/* Persistence snapshots can be requested by more than one CPU after a
 * concurrent write.  Serialize the relatively slow block-device commit while
 * keeping the vnode lock ordering (persistence -> vfs) consistent. */
static xscore_spinlock_t persistence_lock = XSCORE_SPINLOCK_INIT;

static void vfs_lock_release(uintptr_t flags) {
    if (!vfs_locked) return;
    xscore_spin_unlock_irqrestore(&vfs_lock, flags);
}
static uint32_t node_count;
static int vfs_initialized;
static int persistence_ready;
static int persistence_loading;
static uint32_t persistence_generation;
static uint64_t active_superblock_sector;
static uint8_t persistence_payload[AKAOS_FS_PAYLOAD_MAX];
static uint8_t persistence_sector[AKAOS_FS_SECTOR_SIZE];

#define XSCORE_VFS_PROC_PID_BASE UINT32_C(0x1000)
static uint32_t proc_root_node;
static uint32_t proc_self_node;
static uint32_t proc_self_status_node;
static uint32_t proc_processes_node;
static uint32_t proc_mounts_node;
static uint32_t proc_uptime_node;
static uint32_t proc_kmsg_node;
static uint32_t proc_realtime_node;
static uint32_t proc_meminfo_node;
static uint32_t proc_slabinfo_node;
static uint32_t proc_net_node;
static uint32_t proc_net_tcp_node;
static uint32_t proc_net_udp_node;
static uint32_t proc_net_route_node;
static uint32_t proc_net_arp_node;
static uint32_t proc_net_dev_node;
static uint32_t proc_cpuinfo_node;
static uint32_t proc_gpuinfo_node;
static uint32_t proc_pci_node;
static uint32_t proc_usbinfo_node;
static uint32_t proc_bus_usb_dev_node;
static uint32_t proc_asound_node;
static uint32_t proc_asound_cards_node;
static uint32_t proc_acpi_node;
static uint32_t proc_acpi_info_node;
static uint32_t proc_acpi_fadt_node;
static uint32_t proc_acpi_madt_node;
static uint32_t proc_acpi_battery_node;
static uint32_t proc_acpi_thermal_node;
static int procfs_installed;

static void vfs_append_str(char *buffer, size_t capacity, size_t *used,
                           const char *text);
static void vfs_append_u64(char *buffer, size_t capacity, size_t *used,
                           uint64_t value);

#define XSCORE_VFS_MAX_MOUNTS 8

struct xscore_mount_entry {
    int active;
    char source[32];
    char target[XSCORE_VFS_PATH_MAX + 1];
    char fstype[16];
    uint64_t flags;
    uint32_t target_node;
};

static struct xscore_mount_entry mount_table[XSCORE_VFS_MAX_MOUNTS];

static void vfs_copy_str(char *dst, size_t dst_max, const char *src) {
    if (!dst || dst_max == 0) return;
    size_t i = 0;
    if (src) {
        while (src[i] && i + 1 < dst_max) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static void init_mount_table_defaults(void) {
    static int defaults_set = 0;
    if (defaults_set) return;
    defaults_set = 1;

    mount_table[0].active = 1;
    vfs_copy_str(mount_table[0].source, sizeof(mount_table[0].source), "rootfs");
    vfs_copy_str(mount_table[0].target, sizeof(mount_table[0].target), "/");
    vfs_copy_str(mount_table[0].fstype, sizeof(mount_table[0].fstype), "akafs");
    mount_table[0].flags = 0;
    mount_table[0].target_node = 0;

    mount_table[1].active = 1;
    vfs_copy_str(mount_table[1].source, sizeof(mount_table[1].source), "devfs");
    vfs_copy_str(mount_table[1].target, sizeof(mount_table[1].target), "/dev");
    vfs_copy_str(mount_table[1].fstype, sizeof(mount_table[1].fstype), "devfs");
    mount_table[1].flags = 0;
    mount_table[1].target_node = 0;

    mount_table[2].active = 1;
    vfs_copy_str(mount_table[2].source, sizeof(mount_table[2].source), "procfs");
    vfs_copy_str(mount_table[2].target, sizeof(mount_table[2].target), "/proc");
    vfs_copy_str(mount_table[2].fstype, sizeof(mount_table[2].fstype), "procfs");
    mount_table[2].flags = AKAOS_MS_RDONLY;
    mount_table[2].target_node = 0;
}


static void current_credentials(uint32_t *uid, uint32_t *gid) {
    struct xscore_process *process = xscore_process_current();
    if (uid) *uid = xscore_process_effective_uid(process);
    if (gid) *gid = xscore_process_effective_gid(process);
}

static int node_allows(const struct xscore_vnode *node, uint32_t access) {
    uint32_t uid = 0, gid = 0;
    current_credentials(&uid, &gid);
    if (uid == 0) return 1;
    uint32_t shift = uid == node->uid ? 6 : (gid == node->gid ? 3 : 0);
    return (((node->mode >> shift) & access) == access);
}

static void free_vnode_blocks(struct xscore_vnode *node) {
    if (!node) return;
    for (size_t i = 0; i < XSCORE_VFS_DIRECT_BLOCKS; ++i) {
        if (node->block_pages[i] != 0) {
            xscore_pmm_free_page(node->block_pages[i]);
            node->block_pages[i] = 0;
        }
    }
}

static void reset_unlinked_vnode_locked(struct xscore_vnode *node) {
    if (!node || !node->unlinked || node->references != 0) return;
    free_vnode_blocks(node);
    node->type = XSCORE_VNODE_UNUSED;
    node->parent = 0;
    node->name[0] = 0;
    node->data = 0;
    node->size = 0;
    node->mode = 0;
    node->uid = 0;
    node->gid = 0;
    node->is_dynamic = 0;
    node->unlinked = 0;
}

static uint8_t *get_vnode_block_buffer(struct xscore_vnode *node, size_t block_idx, int allocate) {
    if (!node || block_idx >= XSCORE_VFS_DIRECT_BLOCKS) return NULL;
    if (node->block_pages[block_idx] == 0) {
        if (!allocate) return NULL;
        uint64_t page = xscore_pmm_allocate_page();
        if (page == 0) return NULL;
        uint8_t *p = (uint8_t *)(uintptr_t)page;
        for (size_t i = 0; i < XSCORE_VFS_BLOCK_SIZE; ++i) p[i] = 0;
        node->block_pages[block_idx] = page;
    }
    return (uint8_t *)(uintptr_t)node->block_pages[block_idx];
}

static int convert_to_dynamic(struct xscore_vnode *node) {
    if (!node || node->type != XSCORE_VNODE_REGULAR || node->is_dynamic) return 1;
    if (node->size > XSCORE_VFS_FILE_MAX_SIZE) return 0;

    size_t copied = 0;
    while (copied < node->size) {
        size_t block_idx = copied / XSCORE_VFS_BLOCK_SIZE;
        size_t block_off = copied % XSCORE_VFS_BLOCK_SIZE;
        size_t chunk = node->size - copied;
        if (chunk > XSCORE_VFS_BLOCK_SIZE - block_off) {
            chunk = XSCORE_VFS_BLOCK_SIZE - block_off;
        }
        uint8_t *buf = get_vnode_block_buffer(node, block_idx, 1);
        if (!buf) {
            free_vnode_blocks(node);
            return 0;
        }
        for (size_t i = 0; i < chunk; ++i) {
            buf[block_off + i] = node->data[copied + i];
        }
        copied += chunk;
    }
    node->data = NULL;
    node->is_dynamic = 1;
    return 1;
}

static void put_u32(uint8_t *target, uint32_t value) {
    target[0] = (uint8_t)value;
    target[1] = (uint8_t)(value >> 8);
    target[2] = (uint8_t)(value >> 16);
    target[3] = (uint8_t)(value >> 24);
}

static uint32_t get_u32(const uint8_t *source) {
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static uint32_t checksum_bytes(const uint8_t *bytes, size_t length) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static int name_equals(const char *left, const char *right, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != right[index]) return 0;
    }
    return left[length] == 0;
}

static int find_child(uint32_t parent, const char *name, size_t length) {
    for (uint32_t index = 1; index < node_count; ++index) {
        if (nodes[index].type != XSCORE_VNODE_UNUSED &&
            nodes[index].parent == parent &&
            name_equals(nodes[index].name, name, length)) return (int)index;
    }
    return -1;
}

static int split_path(const char *path, char *parent_out, size_t parent_out_max,
                      char *name_out, size_t name_out_max) {
    if (!path || !*path) return 0;
    size_t len = 0;
    while (path[len]) ++len;
    while (len > 1 && path[len - 1] == '/') --len;

    int last_slash = -1;
    for (size_t i = 0; i < len; ++i) {
        if (path[i] == '/') last_slash = (int)i;
    }

    if (last_slash < 0) {
        if (parent_out_max < 2 || name_out_max < len + 1) return 0;
        parent_out[0] = '.';
        parent_out[1] = '\0';
        for (size_t i = 0; i < len; ++i) name_out[i] = path[i];
        name_out[len] = '\0';
        return 1;
    }

    if (last_slash == 0) {
        if (parent_out_max < 2 || name_out_max < len) return 0;
        parent_out[0] = '/';
        parent_out[1] = '\0';
        size_t name_len = len - 1;
        for (size_t i = 0; i < name_len; ++i) name_out[i] = path[1 + i];
        name_out[name_len] = '\0';
        return 1;
    }

    size_t parent_len = (size_t)last_slash;
    if (parent_out_max < parent_len + 1) return 0;
    for (size_t i = 0; i < parent_len; ++i) parent_out[i] = path[i];
    parent_out[parent_len] = '\0';

    size_t name_len = len - (size_t)(last_slash + 1);
    if (name_out_max < name_len + 1) return 0;
    for (size_t i = 0; i < name_len; ++i) name_out[i] = path[last_slash + 1 + i];
    name_out[name_len] = '\0';
    return 1;
}

int xscore_vfs_build_node_path(uint32_t node_id, char *path, size_t capacity) {
    if (!path || capacity < 2) return 0;
    if (node_id >= XSCORE_VFS_PROC_PID_BASE &&
        node_id < XSCORE_VFS_PROC_PID_BASE + 1000) {
        uint64_t pid = node_id - XSCORE_VFS_PROC_PID_BASE;
        char temp[32];
        size_t u = 0;
        const char pfx[] = "/proc/";
        while (pfx[u]) { temp[u] = pfx[u]; ++u; }
        char digits[20];
        size_t d = 0;
        if (pid == 0) digits[d++] = '0';
        while (pid > 0 && d < sizeof(digits)) {
            digits[d++] = (char)('0' + pid % 10);
            pid /= 10;
        }
        while (d > 0 && u < sizeof(temp) - 1) temp[u++] = digits[--d];
        temp[u] = '\0';
        if (u >= capacity) return 0;
        for (size_t i = 0; i <= u; ++i) path[i] = temp[i];
        return 1;
    }
    if (node_id >= XSCORE_VFS_PROC_PID_BASE + 0x1000 &&
        node_id < XSCORE_VFS_PROC_PID_BASE + 0x2000) {
        uint64_t pid = node_id - (XSCORE_VFS_PROC_PID_BASE + 0x1000);
        char temp[40];
        size_t u = 0;
        const char pfx[] = "/proc/";
        while (pfx[u]) { temp[u] = pfx[u]; ++u; }
        char digits[20];
        size_t d = 0;
        if (pid == 0) digits[d++] = '0';
        while (pid > 0 && d < sizeof(digits)) {
            digits[d++] = (char)('0' + pid % 10);
            pid /= 10;
        }
        while (d > 0 && u < sizeof(temp) - 10) temp[u++] = digits[--d];
        const char sfx[] = "/status";
        size_t su = 0;
        while (sfx[su] && u < sizeof(temp) - 1) temp[u++] = sfx[su++];
        temp[u] = '\0';
        if (u >= capacity) return 0;
        for (size_t i = 0; i <= u; ++i) path[i] = temp[i];
        return 1;
    }
    /* Path length bounds the component count; sizing this temporary array by
     * the whole vnode table needlessly consumed half the small syscall stack. */
    uint32_t chain[(XSCORE_VFS_PATH_MAX + 1U) / 2U];
    size_t depth = 0;
    uint32_t current = node_id;
    while (current != 0 && depth < sizeof(chain) / sizeof(chain[0])) {
        if (current >= node_count || current >= XSCORE_VFS_MAX_NODES ||
            nodes[current].type == XSCORE_VNODE_UNUSED)
            return 0;
        chain[depth++] = current;
        current = nodes[current].parent;
    }
    if (current != 0 || capacity < 2) return 0;
    size_t used = 0;
    path[used++] = '/';
    while (depth) {
        const char *name = nodes[chain[--depth]].name;
        size_t length = 0;
        while (name[length]) ++length;
        if (used + length + (depth ? 1 : 0) >= capacity) return 0;
        for (size_t index = 0; index < length; ++index) path[used++] = name[index];
        if (depth) path[used++] = '/';
    }
    path[used] = 0;
    return 1;
}

static int restore_persistent_entry(uint8_t type, const char *path,
                                    uint32_t mode, uint32_t uid, uint32_t gid,
                                    const uint8_t *data, uint32_t size) {
    uint32_t target;
    int resolved = xscore_vfs_resolve_path_at(0, path, &target) == 0;
    if (type == XSCORE_VNODE_DIRECTORY) {
        if (!resolved) {
            if (xscore_vfs_mkdir_at(0, path, mode) != 0) return 0;
            if (xscore_vfs_resolve_path_at(0, path, &target) != 0) return 0;
        }
        if (nodes[target].type != XSCORE_VNODE_DIRECTORY) return 0;
        nodes[target].is_dynamic = 1;
        nodes[target].mode = mode;
        nodes[target].uid = uid;
        nodes[target].gid = gid;
        return 1;
    }
    if (type != XSCORE_VNODE_REGULAR || size > XSCORE_VFS_FILE_MAX_SIZE) return 0;
    if (!resolved) {
        char parent_buf[128];
        char name_buf[XSCORE_VFS_NAME_MAX + 1];
        if (!split_path(path, parent_buf, sizeof(parent_buf), name_buf,
                        sizeof(name_buf))) return 0;
        uint32_t parent;
        if (xscore_vfs_resolve_path_at(0, parent_buf, &parent) != 0 ||
            nodes[parent].type != XSCORE_VNODE_DIRECTORY) return 0;
        int slot = -1;
        for (uint32_t index = 1; index < XSCORE_VFS_MAX_NODES; ++index) {
            if (nodes[index].type == XSCORE_VNODE_UNUSED) {
                slot = (int)index;
                break;
            }
        }
        if (slot < 0) return 0;
        target = (uint32_t)slot;
        struct xscore_vnode *node = &nodes[target];
        node->type = XSCORE_VNODE_REGULAR;
        node->parent = parent;
        size_t name_length = 0;
        while (name_buf[name_length]) {
            node->name[name_length] = name_buf[name_length];
            ++name_length;
        }
        node->name[name_length] = 0;
        if (target >= node_count) node_count = target + 1;
    }
    struct xscore_vnode *node = &nodes[target];
    if (node->type != XSCORE_VNODE_REGULAR) return 0;
    free_vnode_blocks(node);
    node->is_dynamic = 1;
    node->data = NULL;
    node->size = size;
    node->mode = mode;
    node->uid = uid;
    node->gid = gid;

    size_t restored = 0;
    while (restored < size) {
        size_t block_idx = restored / XSCORE_VFS_BLOCK_SIZE;
        size_t block_off = restored % XSCORE_VFS_BLOCK_SIZE;
        size_t chunk = size - restored;
        if (chunk > XSCORE_VFS_BLOCK_SIZE - block_off) {
            chunk = XSCORE_VFS_BLOCK_SIZE - block_off;
        }
        uint8_t *dst = get_vnode_block_buffer(node, block_idx, 1);
        if (!dst) return 0;
        for (size_t i = 0; i < chunk; ++i) {
            dst[block_off + i] = data[restored + i];
        }
        restored += chunk;
    }
    return 1;
}

/* Copy a vnode's contents while vfs_lock is held.  Persistence snapshots
 * must observe one coherent name/size/block view; calling the public read
 * helper here would recursively acquire vfs_lock and deadlock. */
static int persistence_copy_node_data_locked(const struct xscore_vnode *node,
                                             uint8_t *destination,
                                             size_t length) {
    if (!node || !destination || node->type != XSCORE_VNODE_REGULAR) return 0;
    if (length == 0) return 1;
    if (!node->is_dynamic && node->data) {
        for (size_t index = 0; index < length; ++index) {
            destination[index] = node->data[index];
        }
        return 1;
    }
    size_t copied = 0;
    while (copied < length) {
        size_t block_index = copied / XSCORE_VFS_BLOCK_SIZE;
        size_t block_offset = copied % XSCORE_VFS_BLOCK_SIZE;
        size_t chunk = length - copied;
        if (chunk > XSCORE_VFS_BLOCK_SIZE - block_offset) {
            chunk = XSCORE_VFS_BLOCK_SIZE - block_offset;
        }
        uint8_t *source = node->block_pages[block_index]
                              ? (uint8_t *)(uintptr_t)node->block_pages[block_index]
                              : NULL;
        if (source) {
            for (size_t index = 0; index < chunk; ++index) {
                destination[copied + index] = source[block_offset + index];
            }
        } else {
            for (size_t index = 0; index < chunk; ++index) {
                destination[copied + index] = 0;
            }
        }
        copied += chunk;
    }
    return 1;
}

static int persistence_build_payload(size_t *out_used, uint32_t *out_entries) {
    if (!out_used || !out_entries) return 0;
    size_t used = 0;
    uint32_t entries = 0;
    char path[XSCORE_VFS_PATH_MAX + 1];
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    int success = 1;
    for (int pass = 0; pass < 2 && success; ++pass) {
        enum xscore_vnode_type wanted = pass == 0 ? XSCORE_VNODE_DIRECTORY
                                                  : XSCORE_VNODE_REGULAR;
        for (uint32_t index = 1; index < node_count; ++index) {
            struct xscore_vnode *node = &nodes[index];
            if (!node->is_dynamic || node->type != wanted) continue;
            if (!xscore_vfs_build_node_path(index, path, sizeof(path))) {
                success = 0;
                break;
            }
            size_t path_length = 0;
            while (path[path_length]) ++path_length;
            size_t data_length = wanted == XSCORE_VNODE_REGULAR ? node->size : 0;
            size_t record_length = 20 + path_length + data_length;
            if (used + record_length > AKAOS_FS_PAYLOAD_MAX) {
                success = 0;
                break;
            }
            uint8_t *record = &persistence_payload[used];
            record[0] = (uint8_t)wanted;
            record[1] = (uint8_t)path_length;
            record[2] = 0;
            record[3] = 0;
            put_u32(record + 4, node->mode);
            put_u32(record + 8, (uint32_t)data_length);
            put_u32(record + 12, node->uid);
            put_u32(record + 16, node->gid);
            for (size_t item = 0; item < path_length; ++item) {
                record[20 + item] = (uint8_t)path[item];
            }
            if (data_length > 0 && !persistence_copy_node_data_locked(
                    node, &record[20 + path_length], data_length)) {
                success = 0;
                break;
            }
            used += record_length;
            ++entries;
        }
    }
    if (vfs_locked) xscore_spin_unlock_irqrestore(&vfs_lock, lock_flags);
    if (!success) return 0;
    *out_used = used;
    *out_entries = entries;
    return 1;
}

static int persistence_sync_locked(void) {
    if (!persistence_ready || persistence_loading ||
        !xscore_virtio_blk_present()) return 0;

    size_t used = 0;
    uint32_t entries = 0;
    if (!persistence_build_payload(&used, &entries)) return 0;

    uint64_t sectors = (used + AKAOS_FS_SECTOR_SIZE - 1) / AKAOS_FS_SECTOR_SIZE;
    if (AKAOS_FS_PAYLOAD_SECTOR + sectors > xscore_virtio_blk_sector_count()) return 0;
    for (uint64_t sector_index = 0; sector_index < sectors; ++sector_index) {
        for (size_t byte = 0; byte < AKAOS_FS_SECTOR_SIZE; ++byte) {
            size_t offset = (size_t)sector_index * AKAOS_FS_SECTOR_SIZE + byte;
            persistence_sector[byte] = offset < used ? persistence_payload[offset] : 0;
        }
        if (!xscore_bufcache_write(AKAOS_FS_PAYLOAD_SECTOR + sector_index,
                                   persistence_sector)) return 0;
    }

    uint64_t target_sb_sector = (active_superblock_sector == AKAOS_FS_SUPERBLOCK_A_SECTOR)
                                    ? AKAOS_FS_SUPERBLOCK_B_SECTOR
                                    : AKAOS_FS_SUPERBLOCK_A_SECTOR;

    for (size_t byte = 0; byte < sizeof(persistence_sector); ++byte) {
        persistence_sector[byte] = 0;
    }
    static const uint8_t magic[8] = {'A','K','A','F','S','0','0','2'};
    for (size_t byte = 0; byte < sizeof(magic); ++byte) persistence_sector[byte] = magic[byte];
    put_u32(persistence_sector + 8, AKAOS_FS_VERSION);
    put_u32(persistence_sector + 12, ++persistence_generation);
    put_u32(persistence_sector + 16, entries);
    put_u32(persistence_sector + 20, (uint32_t)used);
    put_u32(persistence_sector + 24, checksum_bytes(persistence_payload, used));
    put_u32(persistence_sector + 28, 1); /* State: Committed */
    put_u32(persistence_sector + 32, checksum_bytes(persistence_sector, 32)); /* Header checksum */

    if (!xscore_bufcache_write(target_sb_sector, persistence_sector)) return 0;
    int sync_res = xscore_bufcache_sync();
    if (sync_res) {
        active_superblock_sector = target_sb_sector;
    }
    return sync_res;
}

int xscore_vfs_persistence_sync(void) {
    uintptr_t flags = xscore_spin_lock_irqsave(&persistence_lock);
    int result = persistence_sync_locked();
    xscore_spin_unlock_irqrestore(&persistence_lock, flags);
    return result;
}

struct akafs_superblock_info {
    int valid;
    uint32_t version;
    uint32_t generation;
    uint32_t entries;
    uint32_t payload_size;
    uint32_t payload_checksum;
    uint64_t sector;
};

static int parse_superblock(uint64_t sector, struct akafs_superblock_info *info) {
    if (!info) return 0;
    info->valid = 0;
    info->sector = sector;
    if (!xscore_bufcache_read(sector, persistence_sector)) return 0;

    static const uint8_t magic_v2[8] = {'A','K','A','F','S','0','0','2'};
    static const uint8_t magic_v1[8] = {'A','K','A','F','S','0','0','1'};

    int is_v2 = 1;
    for (size_t i = 0; i < 8; ++i) {
        if (persistence_sector[i] != magic_v2[i]) { is_v2 = 0; break; }
    }
    int is_v1 = 0;
    if (!is_v2) {
        is_v1 = 1;
        for (size_t i = 0; i < 8; ++i) {
            if (persistence_sector[i] != magic_v1[i]) { is_v1 = 0; break; }
        }
    }
    if (!is_v1 && !is_v2) return 0;

    uint32_t version = get_u32(persistence_sector + 8);
    uint32_t generation = get_u32(persistence_sector + 12);
    uint32_t entries = get_u32(persistence_sector + 16);
    uint32_t payload_size = get_u32(persistence_sector + 20);
    uint32_t payload_checksum = get_u32(persistence_sector + 24);

    if (is_v2) {
        uint32_t state = get_u32(persistence_sector + 28);
        uint32_t header_csum = get_u32(persistence_sector + 32);
        if (state != 1) return 0;
        if (checksum_bytes(persistence_sector, 32) != header_csum) return 0;
    }

    if (payload_size > AKAOS_FS_PAYLOAD_MAX) return 0;
    uint64_t payload_start = (is_v1) ? 9 : AKAOS_FS_PAYLOAD_SECTOR;
    uint64_t sectors = (payload_size + AKAOS_FS_SECTOR_SIZE - 1) / AKAOS_FS_SECTOR_SIZE;
    if (payload_start + sectors > xscore_virtio_blk_sector_count()) return 0;

    info->valid = 1;
    info->version = version;
    info->generation = generation;
    info->entries = entries;
    info->payload_size = payload_size;
    info->payload_checksum = payload_checksum;
    return 1;
}

int xscore_vfs_persistence_mount(void) {
    persistence_ready = 0;
    persistence_loading = 0;
    persistence_generation = 0;
    active_superblock_sector = AKAOS_FS_SUPERBLOCK_A_SECTOR;

    if (!xscore_virtio_blk_present() ||
        xscore_virtio_blk_sector_count() <= AKAOS_FS_PAYLOAD_SECTOR) return 0;

    struct akafs_superblock_info sb_a, sb_b;
    parse_superblock(AKAOS_FS_SUPERBLOCK_A_SECTOR, &sb_a);
    parse_superblock(AKAOS_FS_SUPERBLOCK_B_SECTOR, &sb_b);

    struct akafs_superblock_info *selected = NULL;
    if (sb_a.valid && sb_b.valid) {
        selected = (sb_b.generation > sb_a.generation) ? &sb_b : &sb_a;
    } else if (sb_a.valid) {
        selected = &sb_a;
    } else if (sb_b.valid) {
        selected = &sb_b;
    }

    persistence_ready = 1;
    if (!selected) {
        xscore_serial_write("[INFO] akaFS data disk ready (new filesystem)\n");
        return 1;
    }

    active_superblock_sector = selected->sector;
    uint64_t payload_start = (selected->version == 1) ? 9 : AKAOS_FS_PAYLOAD_SECTOR;
    uint64_t sectors = (selected->payload_size + AKAOS_FS_SECTOR_SIZE - 1) / AKAOS_FS_SECTOR_SIZE;

    for (uint64_t sector_index = 0; sector_index < sectors; ++sector_index) {
        if (!xscore_bufcache_read(payload_start + sector_index, persistence_sector)) return 0;
        size_t base = (size_t)sector_index * AKAOS_FS_SECTOR_SIZE;
        size_t count = selected->payload_size - base;
        if (count > AKAOS_FS_SECTOR_SIZE) count = AKAOS_FS_SECTOR_SIZE;
        for (size_t byte = 0; byte < count; ++byte) {
            persistence_payload[base + byte] = persistence_sector[byte];
        }
    }

    if (checksum_bytes(persistence_payload, selected->payload_size) != selected->payload_checksum) {
        xscore_serial_write("[WARN] akaFS payload checksum failed; attempting fallback superblock\n");
        struct akafs_superblock_info *fallback = (selected == &sb_a) ? (sb_b.valid ? &sb_b : NULL) : (sb_a.valid ? &sb_a : NULL);
        if (!fallback) {
            xscore_serial_write("[WARN] No valid fallback superblock available\n");
            return 0;
        }
        selected = fallback;
        active_superblock_sector = selected->sector;
        payload_start = (selected->version == 1) ? 9 : AKAOS_FS_PAYLOAD_SECTOR;
        sectors = (selected->payload_size + AKAOS_FS_SECTOR_SIZE - 1) / AKAOS_FS_SECTOR_SIZE;
        for (uint64_t sector_index = 0; sector_index < sectors; ++sector_index) {
            if (!xscore_bufcache_read(payload_start + sector_index, persistence_sector)) return 0;
            size_t base = (size_t)sector_index * AKAOS_FS_SECTOR_SIZE;
            size_t count = selected->payload_size - base;
            if (count > AKAOS_FS_SECTOR_SIZE) count = AKAOS_FS_SECTOR_SIZE;
            for (size_t byte = 0; byte < count; ++byte) {
                persistence_payload[base + byte] = persistence_sector[byte];
            }
        }
        if (checksum_bytes(persistence_payload, selected->payload_size) != selected->payload_checksum) {
            xscore_serial_write("[WARN] Fallback akaFS checksum also failed; persistent files not loaded\n");
            return 0;
        }
    }

    persistence_loading = 1;
    size_t offset = 0;
    uint32_t loaded = 0;
    char path[XSCORE_VFS_PATH_MAX + 1];

    if (selected->version == 1) {
        while (loaded < selected->entries && offset + 12 <= selected->payload_size) {
            const uint8_t *record = &persistence_payload[offset];
            uint8_t type = record[0];
            uint8_t path_length = record[1];
            uint32_t mode = get_u32(record + 4);
            uint32_t size = get_u32(record + 8);
            size_t record_length = 12 + (size_t)path_length + size;
            if (path_length == 0 || path_length > XSCORE_VFS_PATH_MAX ||
                offset + record_length > selected->payload_size ||
                (type == XSCORE_VNODE_DIRECTORY && size != 0)) break;
            for (uint8_t byte = 0; byte < path_length; ++byte) {
                path[byte] = (char)record[12 + byte];
            }
            path[path_length] = 0;
            if (!restore_persistent_entry(type, path, mode, 0, 0, record + 12 + path_length, size)) break;
            offset += record_length;
            ++loaded;
        }
    } else {
        while (loaded < selected->entries && offset + 20 <= selected->payload_size) {
            const uint8_t *record = &persistence_payload[offset];
            uint8_t type = record[0];
            uint8_t path_length = record[1];
            uint32_t mode = get_u32(record + 4);
            uint32_t size = get_u32(record + 8);
            uint32_t uid = get_u32(record + 12);
            uint32_t gid = get_u32(record + 16);
            size_t record_length = 20 + (size_t)path_length + size;
            if (path_length == 0 || path_length > XSCORE_VFS_PATH_MAX ||
                offset + record_length > selected->payload_size ||
                (type == XSCORE_VNODE_DIRECTORY && size != 0)) break;
            for (uint8_t byte = 0; byte < path_length; ++byte) {
                path[byte] = (char)record[20 + byte];
            }
            path[path_length] = 0;
            if (!restore_persistent_entry(type, path, mode, uid, gid, record + 20 + path_length, size)) break;
            offset += record_length;
            ++loaded;
        }
    }

    persistence_loading = 0;
    if (loaded != selected->entries || offset != selected->payload_size) {
        xscore_serial_write("[WARN] akaFS payload is invalid; some files were not loaded\n");
        return 0;
    }
    persistence_generation = selected->generation;
    xscore_serial_write("[INFO] akaFS persistent files loaded (dual-superblock journal active)\n");
    return 1;
}

int xscore_vfs_resolve_path_at(uint32_t cwd_node, const char *path,
                               uint32_t *out_node) {
    if (!vfs_initialized || !path || !*path) return -AKAOS_EINVAL;
    if (cwd_node >= XSCORE_VFS_MAX_NODES &&
        !(cwd_node >= XSCORE_VFS_PROC_PID_BASE &&
          cwd_node < XSCORE_VFS_PROC_PID_BASE + 1000)) {
        cwd_node = 0;
    } else if (cwd_node < XSCORE_VFS_MAX_NODES &&
               nodes[cwd_node].type != XSCORE_VNODE_DIRECTORY) {
        cwd_node = 0;
    }
    uint32_t current = (path[0] == '/') ? 0 : cwd_node;
    const char *cursor = (path[0] == '/') ? path + 1 : path;

    while (*cursor) {
        while (*cursor == '/') ++cursor;
        if (!*cursor) break;
        const char *comp = cursor;
        size_t len = 0;
        while (cursor[len] && cursor[len] != '/') ++len;
        if (len > XSCORE_VFS_NAME_MAX) return -AKAOS_EINVAL;

        if (len == 1 && comp[0] == '.') {
            /* self */
        } else if (len == 2 && comp[0] == '.' && comp[1] == '.') {
            /* parent */
            if (current >= XSCORE_VFS_PROC_PID_BASE &&
                current < XSCORE_VFS_PROC_PID_BASE + 1000) {
                current = proc_root_node;
            } else if (current < XSCORE_VFS_MAX_NODES) {
                current = nodes[current].parent;
            }
        } else if (procfs_installed && current == proc_root_node) {
            if (name_equals("self", comp, len)) {
                current = proc_self_node;
            } else if (name_equals("processes", comp, len)) {
                current = proc_processes_node;
            } else if (name_equals("mounts", comp, len)) {
                current = proc_mounts_node;
            } else if (name_equals("uptime", comp, len)) {
                current = proc_uptime_node;
            } else if (name_equals("realtime", comp, len)) {
                current = proc_realtime_node;
            } else if (name_equals("meminfo", comp, len)) {
                current = proc_meminfo_node;
            } else if (name_equals("net", comp, len)) {
                current = proc_net_node;
            } else if (name_equals("cpuinfo", comp, len)) {
                current = proc_cpuinfo_node;
            } else if (name_equals("gpuinfo", comp, len)) {
                current = proc_gpuinfo_node;
            } else if (name_equals("pci", comp, len)) {
                current = proc_pci_node;
            } else if (name_equals("asound", comp, len)) {
                current = proc_asound_node;
            } else if (name_equals("acpi", comp, len)) {
                current = proc_acpi_node;
            } else {
                uint64_t pid = 0;
                int all_digits = 1;
                for (size_t i = 0; i < len; ++i) {
                    if (comp[i] < '0' || comp[i] > '9') { all_digits = 0; break; }
                    pid = pid * 10U + (uint64_t)(comp[i] - '0');
                }
                if (all_digits && pid > 0 && xscore_process_pid_info(pid, NULL, NULL, NULL)) {
                    current = XSCORE_VFS_PROC_PID_BASE + (uint32_t)pid;
                } else {
                    int child = find_child(current, comp, len);
                    if (child < 0) return -AKAOS_ENOENT;
                    current = (uint32_t)child;
                }
            }
        } else if (procfs_installed && current == proc_net_node) {
            if (name_equals("tcp", comp, len)) {
                current = proc_net_tcp_node;
            } else if (name_equals("udp", comp, len)) {
                current = proc_net_udp_node;
            } else if (name_equals("route", comp, len)) {
                current = proc_net_route_node;
            } else if (name_equals("arp", comp, len)) {
                current = proc_net_arp_node;
            } else if (name_equals("dev", comp, len)) {
                current = proc_net_dev_node;
            } else {
                int child = find_child(current, comp, len);
                if (child < 0) return -AKAOS_ENOENT;
                current = (uint32_t)child;
            }
        } else if (procfs_installed && current == proc_asound_node) {
            if (name_equals("cards", comp, len)) {
                current = proc_asound_cards_node;
            } else {
                int child = find_child(current, comp, len);
                if (child < 0) return -AKAOS_ENOENT;
                current = (uint32_t)child;
            }
        } else if (procfs_installed && current == proc_acpi_node) {
            if (name_equals("info", comp, len)) {
                current = proc_acpi_info_node;
            } else if (name_equals("fadt", comp, len)) {
                current = proc_acpi_fadt_node;
            } else if (name_equals("madt", comp, len)) {
                current = proc_acpi_madt_node;
            } else {
                int child = find_child(current, comp, len);
                if (child < 0) return -AKAOS_ENOENT;
                current = (uint32_t)child;
            }
        } else if (procfs_installed &&
                   current >= XSCORE_VFS_PROC_PID_BASE &&
                   current < XSCORE_VFS_PROC_PID_BASE + 1000) {
            uint64_t pid = current - XSCORE_VFS_PROC_PID_BASE;
            if (name_equals("status", comp, len)) {
                current = XSCORE_VFS_PROC_PID_BASE + 0x1000 + (uint32_t)pid;
            } else {
                return -AKAOS_ENOENT;
            }
        } else if (procfs_installed && current == proc_self_node) {
            if (name_equals("status", comp, len)) {
                current = proc_self_status_node;
            } else {
                return -AKAOS_ENOENT;
            }
        } else if (procfs_installed &&
                   (current == proc_self_status_node ||
                    current == proc_processes_node ||
                    current == proc_mounts_node ||
                    (current >= XSCORE_VFS_PROC_PID_BASE + 0x1000 &&
                     current < XSCORE_VFS_PROC_PID_BASE + 0x2000))) {
            return -AKAOS_ENOTDIR;
        } else {
            if (nodes[current].type != XSCORE_VNODE_DIRECTORY) return -AKAOS_ENOTDIR;
            if (!node_allows(&nodes[current], 1)) return -AKAOS_EACCES;
            int child = find_child(current, comp, len);
            if (child < 0) return -AKAOS_ENOENT;
            current = (uint32_t)child;
        }
        cursor += len;
    }
    if (out_node) *out_node = current;
    return 0;
}

int xscore_vfs_initialize(void) {
    vfs_locked = 1;
    persistence_ready = 0;
    persistence_loading = 0;
    persistence_generation = 0;
    active_superblock_sector = AKAOS_FS_SUPERBLOCK_A_SECTOR;
    for (uint32_t index = 0; index < XSCORE_VFS_MAX_NODES; ++index) {
        free_vnode_blocks(&nodes[index]);
        nodes[index].type = XSCORE_VNODE_UNUSED;
        nodes[index].references = 0;
        nodes[index].unlinked = 0;
        nodes[index].uid = 0;
        nodes[index].gid = 0;
    }
    nodes[0].type = XSCORE_VNODE_DIRECTORY;
    nodes[0].parent = 0;
    nodes[0].name[0] = 0;
    nodes[0].data = 0;
    nodes[0].size = 0;
    nodes[0].uid = 0;
    nodes[0].gid = 0;
    nodes[0].is_dynamic = 0;
    nodes[0].references = 0;
    nodes[0].unlinked = 0;
    nodes[0].mode = AKAOS_MODE_DIRECTORY | 0755;
    node_count = 1;
    vfs_initialized = 1;
    return 1;
}

int xscore_vfs_add_directory(const char *path, uint32_t mode) {
    return xscore_vfs_mkdir_at(0, path, mode & UINT32_C(07777)) == 0;
}

int xscore_vfs_add_file(const char *path, const uint8_t *data, size_t size,
                        uint32_t mode) {
    if (!vfs_initialized || node_count >= XSCORE_VFS_MAX_NODES) {
        xscore_serial_write("[FATAL] The file system has no room for ");
        xscore_serial_write(path);
        xscore_serial_write("\n");
        return 0;
    }
    char parent_buf[128];
    char name_buf[XSCORE_VFS_NAME_MAX + 1];
    if (!split_path(path, parent_buf, sizeof(parent_buf), name_buf, sizeof(name_buf))) {
        return 0;
    }
    uint32_t parent_node;
    if (xscore_vfs_resolve_path_at(0, parent_buf, &parent_node) != 0 ||
        nodes[parent_node].type != XSCORE_VNODE_DIRECTORY) {
        xscore_serial_write("[FATAL] The parent directory of ");
        xscore_serial_write(path);
        xscore_serial_write(" is missing: ");
        xscore_serial_write(parent_buf);
        xscore_serial_write("\n");
        return 0;
    }
    size_t name_len = 0;
    while (name_buf[name_len]) ++name_len;
    if (find_child(parent_node, name_buf, name_len) >= 0) {
        xscore_serial_write("[FATAL] The file ");
        xscore_serial_write(path);
        xscore_serial_write(" is already present\n");
        return 0;
    }

    int slot = -1;
    for (uint32_t i = 0; i < XSCORE_VFS_MAX_NODES; ++i) {
        if (nodes[i].type == XSCORE_VNODE_UNUSED) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) return 0;

    struct xscore_vnode *node = &nodes[slot];
    free_vnode_blocks(node);
    node->type = XSCORE_VNODE_REGULAR;
    node->parent = parent_node;
    for (size_t i = 0; i < name_len; ++i) node->name[i] = name_buf[i];
    node->name[name_len] = 0;
    node->data = data;
    node->size = size;
    node->uid = 0;
    node->gid = 0;
    node->is_dynamic = 0;
    node->references = 0;
    node->unlinked = 0;
    node->mode = AKAOS_MODE_REGULAR | (mode & UINT32_C(07777));
    if ((uint32_t)slot >= node_count) node_count = (uint32_t)slot + 1;
    return 1;
}

int xscore_vfs_add_character_device(const char *path, uint32_t mode) {
    if (!vfs_initialized || node_count >= XSCORE_VFS_MAX_NODES) return 0;
    char parent_buf[128];
    char name_buf[XSCORE_VFS_NAME_MAX + 1];
    if (!split_path(path, parent_buf, sizeof(parent_buf), name_buf,
                    sizeof(name_buf))) return 0;
    uint32_t parent_node;
    if (xscore_vfs_resolve_path_at(0, parent_buf, &parent_node) != 0 ||
        nodes[parent_node].type != XSCORE_VNODE_DIRECTORY) return 0;
    size_t name_len = 0;
    while (name_buf[name_len]) ++name_len;
    if (find_child(parent_node, name_buf, name_len) >= 0) return 0;

    int slot = -1;
    for (uint32_t index = 1; index < XSCORE_VFS_MAX_NODES; ++index) {
        if (nodes[index].type == XSCORE_VNODE_UNUSED) {
            slot = (int)index;
            break;
        }
    }
    if (slot < 0) return 0;

    struct xscore_vnode *node = &nodes[slot];
    free_vnode_blocks(node);
    node->type = XSCORE_VNODE_CHARACTER;
    node->parent = parent_node;
    for (size_t index = 0; index < name_len; ++index) {
        node->name[index] = name_buf[index];
    }
    node->name[name_len] = 0;
    node->data = NULL;
    node->size = 0;
    node->uid = 0;
    node->gid = 0;
    node->is_dynamic = 0;
    node->references = 0;
    node->unlinked = 0;
    node->mode = AKAOS_MODE_CHARACTER | (mode & UINT32_C(07777));
    if ((uint32_t)slot >= node_count) node_count = (uint32_t)slot + 1;
    return 1;
}

/* Internal PTY allocation: reset stale ownership/mode on every reuse and
 * pin the vnode until the last master/slave description closes. */
int xscore_vfs_prepare_pts(uint32_t number, uint32_t *node_id) {
    if (number > 9U || !node_id) return -AKAOS_EINVAL;
    char path[] = "/dev/pts/0";
    path[9] = (char)('0' + number);
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    uint32_t target;
    int error = xscore_vfs_resolve_path_at(0, path, &target);
    if (error == -AKAOS_ENOENT) {
        error = xscore_vfs_add_character_device(path, 0620)
                    ? xscore_vfs_resolve_path_at(0, path, &target)
                    : -AKAOS_ENFILE;
    }
    if (error == 0) {
        if (target >= XSCORE_VFS_MAX_NODES ||
            nodes[target].type != XSCORE_VNODE_CHARACTER) {
            error = -AKAOS_ENODEV;
        } else {
            struct xscore_vnode *node = &nodes[target];
            current_credentials(&node->uid, &node->gid);
            node->mode = AKAOS_MODE_CHARACTER | 0620;
            ++node->references;
            *node_id = target;
        }
    }
    vfs_lock_release(lock_flags);
    return error;
}

int xscore_vfs_pts_access(uint32_t node_id, uint32_t access) {
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    int error = -AKAOS_ENODEV;
    if (node_id < XSCORE_VFS_MAX_NODES &&
        nodes[node_id].type == XSCORE_VNODE_CHARACTER &&
        !nodes[node_id].unlinked)
        error = node_allows(&nodes[node_id], access) ? 0 : -AKAOS_EACCES;
    vfs_lock_release(lock_flags);
    return error;
}

int xscore_vfs_install_devfs(void) {
    if (!xscore_vfs_add_directory("/dev", 0755)) return 0;
    if (!xscore_vfs_add_directory("/dev/input", 0755)) return 0;
    if (!xscore_vfs_add_directory("/dev/pts", 0755)) return 0;

    uint32_t node = 0;
    if (xscore_vfs_resolve_path_at(0, "/dev", &node) != 0) return 0;
    nodes[node].is_dynamic = 0;
    if (xscore_vfs_resolve_path_at(0, "/dev/input", &node) != 0) return 0;
    nodes[node].is_dynamic = 0;
    if (xscore_vfs_resolve_path_at(0, "/dev/pts", &node) != 0) return 0;
    nodes[node].is_dynamic = 0;

    xscore_audio_initialize();
    xscore_net_initialize();

    if (!xscore_vfs_add_character_device("/dev/tty1", 0620) ||
        !xscore_vfs_add_character_device("/dev/null", 0666) ||
        !xscore_vfs_add_character_device("/dev/zero", 0666) ||
        !xscore_vfs_add_character_device("/dev/fb0", 0600) ||
        !xscore_vfs_add_character_device("/dev/input/event0", 0600) ||
        !xscore_vfs_add_character_device("/dev/ptmx", 0666) ||
        !xscore_vfs_add_character_device("/dev/audio", 0666) ||
        !xscore_vfs_add_character_device("/dev/dsp", 0666) ||
        !xscore_vfs_add_character_device("/dev/gpu0", 0666) ||
        !xscore_vfs_add_character_device("/dev/net", 0666))
        return 0;
#if defined(__x86_64__) || defined(__i386__)
    return xscore_vfs_add_character_device("/dev/random", 0666) &&
           xscore_vfs_add_character_device("/dev/urandom", 0666);
#else
    return 1;
#endif
}

int xscore_vfs_install_procfs(void) {
    static const uint8_t empty[] = "";
    if (!xscore_vfs_add_directory("/proc", 0555) ||
        !xscore_vfs_add_directory("/proc/self", 0555) ||
        !xscore_vfs_add_file("/proc/self/status", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/processes", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/mounts", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/uptime", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/kmsg", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/realtime", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/meminfo", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/slabinfo", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/cpuinfo", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/gpuinfo", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/pci", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/usbinfo", empty, 0, 0444) ||
        !xscore_vfs_add_directory("/proc/bus", 0555) ||
        !xscore_vfs_add_directory("/proc/bus/usb", 0555) ||
        !xscore_vfs_add_file("/proc/bus/usb/devices", empty, 0, 0444) ||
        !xscore_vfs_add_directory("/proc/asound", 0555) ||
        !xscore_vfs_add_file("/proc/asound/cards", empty, 0, 0444) ||
        !xscore_vfs_add_directory("/proc/acpi", 0555) ||
        !xscore_vfs_add_file("/proc/acpi/info", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/acpi/fadt", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/acpi/madt", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/acpi/battery", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/acpi/thermal", empty, 0, 0444) ||
        !xscore_vfs_add_directory("/proc/net", 0555) ||
        !xscore_vfs_add_file("/proc/net/tcp", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/net/udp", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/net/route", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/net/arp", empty, 0, 0444) ||
        !xscore_vfs_add_file("/proc/net/dev", empty, 0, 0444)) {
        return 0;
    }
    (void)xscore_vfs_resolve_path_at(0, "/proc", &proc_root_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/self", &proc_self_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/self/status", &proc_self_status_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/processes", &proc_processes_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/mounts", &proc_mounts_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/uptime", &proc_uptime_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/kmsg", &proc_kmsg_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/realtime", &proc_realtime_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/meminfo", &proc_meminfo_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/slabinfo", &proc_slabinfo_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/cpuinfo", &proc_cpuinfo_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/gpuinfo", &proc_gpuinfo_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/pci", &proc_pci_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/usbinfo", &proc_usbinfo_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/bus/usb/devices", &proc_bus_usb_dev_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/asound", &proc_asound_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/asound/cards", &proc_asound_cards_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/acpi", &proc_acpi_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/acpi/info", &proc_acpi_info_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/acpi/fadt", &proc_acpi_fadt_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/acpi/madt", &proc_acpi_madt_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/acpi/battery", &proc_acpi_battery_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/acpi/thermal", &proc_acpi_thermal_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/net", &proc_net_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/net/tcp", &proc_net_tcp_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/net/udp", &proc_net_udp_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/net/route", &proc_net_route_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/net/arp", &proc_net_arp_node);
    (void)xscore_vfs_resolve_path_at(0, "/proc/net/dev", &proc_net_dev_node);
    nodes[proc_root_node].is_dynamic = 0;
    nodes[proc_self_node].is_dynamic = 0;
    nodes[proc_self_status_node].is_dynamic = 0;
    nodes[proc_processes_node].is_dynamic = 0;
    nodes[proc_mounts_node].is_dynamic = 0;
    nodes[proc_uptime_node].is_dynamic = 0;
    nodes[proc_kmsg_node].is_dynamic = 0;
    nodes[proc_realtime_node].is_dynamic = 0;
    nodes[proc_meminfo_node].is_dynamic = 0;
    nodes[proc_slabinfo_node].is_dynamic = 0;
    nodes[proc_cpuinfo_node].is_dynamic = 0;
    nodes[proc_gpuinfo_node].is_dynamic = 0;
    nodes[proc_pci_node].is_dynamic = 0;
    nodes[proc_asound_node].is_dynamic = 0;
    nodes[proc_asound_cards_node].is_dynamic = 0;
    nodes[proc_acpi_node].is_dynamic = 0;
    nodes[proc_acpi_info_node].is_dynamic = 0;
    nodes[proc_acpi_fadt_node].is_dynamic = 0;
    nodes[proc_acpi_madt_node].is_dynamic = 0;
    nodes[proc_acpi_battery_node].is_dynamic = 0;
    nodes[proc_acpi_thermal_node].is_dynamic = 0;
    nodes[proc_net_node].is_dynamic = 0;
    nodes[proc_net_tcp_node].is_dynamic = 0;
    nodes[proc_net_udp_node].is_dynamic = 0;
    nodes[proc_net_route_node].is_dynamic = 0;
    nodes[proc_net_arp_node].is_dynamic = 0;
    nodes[proc_net_dev_node].is_dynamic = 0;
    procfs_installed = 1;
    init_mount_table_defaults();
    return 1;
}

enum xscore_proc_kind xscore_vfs_proc_kind_pid_at(uint32_t cwd_node,
                                                  const char *path,
                                                  uint64_t *out_pid) {
    uint32_t target = 0;
    if (xscore_vfs_resolve_path_at(cwd_node, path, &target) != 0) {
        return XSCORE_PROC_NONE;
    }
    if (procfs_installed) {
        if (target == proc_root_node) return XSCORE_PROC_ROOT;
        if (target == proc_self_node) return XSCORE_PROC_SELF_DIR;
        if (target == proc_self_status_node) return XSCORE_PROC_SELF_STATUS;
        if (target == proc_processes_node) return XSCORE_PROC_PROCESSES;
        if (target == proc_mounts_node) return XSCORE_PROC_MOUNTS;
        if (target == proc_uptime_node) return XSCORE_PROC_UPTIME;
        if (target == proc_realtime_node) return XSCORE_PROC_REALTIME;
        if (target == proc_meminfo_node) return XSCORE_PROC_MEMINFO;
        if (target == proc_slabinfo_node) return XSCORE_PROC_SLABINFO;
        if (target == proc_cpuinfo_node) return XSCORE_PROC_CPUINFO;
        if (target == proc_gpuinfo_node) return XSCORE_PROC_GPUINFO;
        if (target == proc_pci_node) return XSCORE_PROC_PCI;
        if (target == proc_asound_node) return XSCORE_PROC_ASOUND_DIR;
        if (target == proc_asound_cards_node) return XSCORE_PROC_ASOUND_CARDS;
        if (target == proc_acpi_node) return XSCORE_PROC_ACPI_DIR;
        if (target == proc_acpi_info_node) return XSCORE_PROC_ACPI_INFO;
        if (target == proc_acpi_fadt_node) return XSCORE_PROC_ACPI_FADT;
        if (target == proc_acpi_madt_node) return XSCORE_PROC_ACPI_MADT;
        if (target == proc_acpi_battery_node) return XSCORE_PROC_ACPI_BATTERY;
        if (target == proc_acpi_thermal_node) return XSCORE_PROC_ACPI_THERMAL;
        if (target == proc_net_node) return XSCORE_PROC_NET_DIR;
        if (target == proc_net_tcp_node) return XSCORE_PROC_NET_TCP;
        if (target == proc_net_udp_node) return XSCORE_PROC_NET_UDP;
        if (target == proc_net_route_node) return XSCORE_PROC_NET_ROUTE;
        if (target == proc_net_arp_node) return XSCORE_PROC_NET_ARP;
        if (target == proc_net_dev_node) return XSCORE_PROC_NET_DEV;
        if (target >= XSCORE_VFS_PROC_PID_BASE &&
            target < XSCORE_VFS_PROC_PID_BASE + 1000) {
            if (out_pid) *out_pid = target - XSCORE_VFS_PROC_PID_BASE;
            return XSCORE_PROC_PID_DIR;
        }
        if (target >= XSCORE_VFS_PROC_PID_BASE + 0x1000 &&
            target < XSCORE_VFS_PROC_PID_BASE + 0x2000) {
            if (out_pid) *out_pid = target - (XSCORE_VFS_PROC_PID_BASE + 0x1000);
            return XSCORE_PROC_PID_STATUS;
        }
    }
    return XSCORE_PROC_NONE;
}

enum xscore_proc_kind xscore_vfs_proc_kind_at(uint32_t cwd_node,
                                              const char *path) {
    return xscore_vfs_proc_kind_pid_at(cwd_node, path, NULL);
}

enum xscore_device_kind xscore_vfs_device_kind_at(uint32_t cwd_node,
                                                   const char *path) {
    uint32_t target = 0;
    if (xscore_vfs_resolve_path_at(cwd_node, path, &target) != 0 ||
        target >= node_count || nodes[target].type != XSCORE_VNODE_CHARACTER) {
        return XSCORE_DEVICE_NONE;
    }
    char absolute[XSCORE_VFS_PATH_MAX + 1];
    if (!xscore_vfs_build_node_path(target, absolute, sizeof(absolute))) {
        return XSCORE_DEVICE_NONE;
    }
    static const struct {
        const char *path;
        enum xscore_device_kind kind;
    } devices[] = {
        {"/dev/tty1", XSCORE_DEVICE_TTY1},
        {"/dev/null", XSCORE_DEVICE_NULL},
        {"/dev/zero", XSCORE_DEVICE_ZERO},
        {"/dev/random", XSCORE_DEVICE_RANDOM},
        {"/dev/urandom", XSCORE_DEVICE_RANDOM},
        {"/dev/fb0", XSCORE_DEVICE_FRAMEBUFFER},
        {"/dev/input/event0", XSCORE_DEVICE_INPUT_EVENT0},
        {"/dev/ptmx", XSCORE_DEVICE_PTMX},
        {"/dev/audio", XSCORE_DEVICE_AUDIO},
        {"/dev/dsp", XSCORE_DEVICE_AUDIO},
        {"/dev/gpu0", XSCORE_DEVICE_GPU0},
        {"/dev/nvme0n1", XSCORE_DEVICE_NVME0N1},
        {"/dev/net", XSCORE_DEVICE_NET},
    };
    for (size_t index = 0; index < sizeof(devices) / sizeof(devices[0]);
         ++index) {
        size_t length = 0;
        while (devices[index].path[length]) ++length;
        if (name_equals(absolute, devices[index].path, length)) {
            return devices[index].kind;
        }
    }
    if (absolute[0] == '/' && absolute[1] == 'd' && absolute[2] == 'e' &&
        absolute[3] == 'v' && absolute[4] == '/' && absolute[5] == 'p' &&
        absolute[6] == 't' && absolute[7] == 's' && absolute[8] == '/' &&
        absolute[9] >= '0' && absolute[9] <= '9') {
        return XSCORE_DEVICE_PTS;
    }
    return XSCORE_DEVICE_NONE;
}

int xscore_vfs_device_number_at(uint32_t cwd_node, const char *path,
                                uint32_t *number, uint32_t *node_id) {
    uint32_t target = 0;
    char absolute[XSCORE_VFS_PATH_MAX + 1];
    if (!number || !node_id ||
        xscore_vfs_resolve_path_at(cwd_node, path, &target) != 0 ||
        !xscore_vfs_build_node_path(target, absolute, sizeof(absolute))) return 0;
    const char prefix[] = "/dev/pts/";
    size_t index = 0;
    while (prefix[index] && absolute[index] == prefix[index]) ++index;
    if (prefix[index] != '\0' || absolute[index] < '0' ||
        absolute[index] > '9') return 0;
    uint32_t value = 0;
    while (absolute[index] >= '0' && absolute[index] <= '9') {
        value = value * 10U + (uint32_t)(absolute[index++] - '0');
    }
    if (absolute[index] != '\0') return 0;
    *number = value;
    *node_id = target;
    return 1;
}

int xscore_vfs_lookup_file_at(uint32_t cwd_node, const char *path,
                              const uint8_t **data, size_t *size) {
    if (!vfs_initialized || !path || !*path) return 0;
    uint32_t target;
    if (xscore_vfs_resolve_path_at(cwd_node, path, &target) != 0) return 0;
    if (nodes[target].type != XSCORE_VNODE_REGULAR) return 0;
    if (!node_allows(&nodes[target], 1)) return 0;
    if (data) *data = nodes[target].data;
    if (size) *size = nodes[target].size;
    return 1;
}

int xscore_vfs_node_acquire(uint32_t node_id) {
    if (node_id >= XSCORE_VFS_MAX_NODES) return 0;
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    struct xscore_vnode *node = &nodes[node_id];
    /* An unlinked vnode may still be acquired by an existing file/image
     * reference (fork or descriptor duplication). It remains unreachable by
     * path resolution, so this does not reopen the removed name. */
    int valid = node->type != XSCORE_VNODE_UNUSED &&
                (!node->unlinked || node->references != 0);
    if (valid) ++node->references;
    vfs_lock_release(lock_flags);
    return valid;
}

void xscore_vfs_node_release(uint32_t node_id) {
    if (node_id >= XSCORE_VFS_MAX_NODES) return;
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    struct xscore_vnode *node = &nodes[node_id];
    if (node->references > 0) --node->references;
    reset_unlinked_vnode_locked(node);
    vfs_lock_release(lock_flags);
}

int xscore_vfs_lookup_file_ref_at(uint32_t cwd_node, const char *path,
                                  const uint8_t **data, size_t *size,
                                  uint32_t *node_id) {
    if (!vfs_initialized || !path || !*path) return 0;
    uint32_t target = 0;
    if (xscore_vfs_resolve_path_at(cwd_node, path, &target) != 0 ||
        target >= XSCORE_VFS_MAX_NODES) return 0;
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    struct xscore_vnode *node = &nodes[target];
    if (node->type != XSCORE_VNODE_REGULAR || node->unlinked ||
        !node_allows(node, 1)) {
        vfs_lock_release(lock_flags);
        return 0;
    }
    ++node->references;
    if (data) *data = node->data;
    if (size) *size = node->size;
    if (node_id) *node_id = target;
    vfs_lock_release(lock_flags);
    return 1;
}

int xscore_vfs_lookup_node(uint32_t node, const uint8_t **data, size_t *size) {
    if (!vfs_initialized || node >= node_count) return 0;
    if (nodes[node].type != XSCORE_VNODE_REGULAR) return 0;
    if (data) *data = nodes[node].data;
    if (size) *size = nodes[node].size;
    return 1;
}

int xscore_vfs_lookup_file(const char *path, const uint8_t **data,
                           size_t *size) {
    return xscore_vfs_lookup_file_at(0, path, data, size);
}

int xscore_vfs_mkdir_at(uint32_t cwd_node, const char *path, uint32_t mode) {
    if (!vfs_initialized || !path || !*path) return -AKAOS_EINVAL;
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    uint32_t existing;
    if (xscore_vfs_resolve_path_at(cwd_node, path, &existing) == 0) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EEXIST;
    }
    char parent_buf[128];
    char name_buf[XSCORE_VFS_NAME_MAX + 1];
    if (!split_path(path, parent_buf, sizeof(parent_buf), name_buf, sizeof(name_buf))) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EINVAL;
    }
    uint32_t parent_node;
    int err = xscore_vfs_resolve_path_at(cwd_node, parent_buf, &parent_node);
    if (err != 0) {
        vfs_lock_release(lock_flags);
        return err;
    }
    if (nodes[parent_node].type != XSCORE_VNODE_DIRECTORY) {
        vfs_lock_release(lock_flags);
        return -AKAOS_ENOTDIR;
    }
    if (!node_allows(&nodes[parent_node], 3)) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EACCES;
    }

    int slot = -1;
    for (uint32_t i = 0; i < XSCORE_VFS_MAX_NODES; ++i) {
        if (nodes[i].type == XSCORE_VNODE_UNUSED) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) {
        vfs_lock_release(lock_flags);
        return -AKAOS_ENFILE;
    }

    struct xscore_vnode *node = &nodes[slot];
    free_vnode_blocks(node);
    node->type = XSCORE_VNODE_DIRECTORY;
    node->parent = parent_node;
    size_t name_len = 0;
    while (name_buf[name_len] && name_len < XSCORE_VFS_NAME_MAX) {
        node->name[name_len] = name_buf[name_len];
        ++name_len;
    }
    node->name[name_len] = 0;
    node->data = 0;
    node->size = 0;
    current_credentials(&node->uid, &node->gid);
    node->is_dynamic = 1;
    node->mode = AKAOS_MODE_DIRECTORY | (mode & 0777);
    node->references = 0;
    node->unlinked = 0;
    if ((uint32_t)slot >= node_count) node_count = (uint32_t)slot + 1;
    int sync_needed = persistence_ready && !persistence_loading;
    vfs_lock_release(lock_flags);
    int sync_failed = sync_needed && !xscore_vfs_persistence_sync();
    return sync_failed ? -AKAOS_EIO : 0;
}

int xscore_vfs_unlink_at(uint32_t cwd_node, const char *path) {
    if (!vfs_initialized || !path || !*path) return -AKAOS_EINVAL;
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    uint32_t target;
    int err = xscore_vfs_resolve_path_at(cwd_node, path, &target);
    if (err != 0) {
        vfs_lock_release(lock_flags);
        return err;
    }
    if (target == 0) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EPERM;
    }
    if (!node_allows(&nodes[nodes[target].parent], 3)) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EACCES;
    }
    if (nodes[target].type == XSCORE_VNODE_DIRECTORY) {
        for (uint32_t i = 1; i < node_count; ++i) {
            if (nodes[i].type != XSCORE_VNODE_UNUSED && nodes[i].parent == target) {
                vfs_lock_release(lock_flags);
                return -AKAOS_ENOTEMPTY;
            }
        }
    }

    if (nodes[target].references != 0) {
        /* Preserve an open file description's backing pages after unlink.
         * New path lookups cannot find the empty name; the final descriptor
         * release performs the actual slot reclamation. */
        nodes[target].unlinked = 1;
        nodes[target].parent = 0;
        nodes[target].name[0] = 0;
    } else {
        free_vnode_blocks(&nodes[target]);
        nodes[target].type = XSCORE_VNODE_UNUSED;
        nodes[target].name[0] = 0;
        nodes[target].size = 0;
        nodes[target].data = 0;
        nodes[target].uid = 0;
        nodes[target].gid = 0;
        nodes[target].is_dynamic = 0;
        nodes[target].unlinked = 0;
    }
    int sync_needed = persistence_ready && !persistence_loading;
    vfs_lock_release(lock_flags);
    int sync_failed = sync_needed && !xscore_vfs_persistence_sync();
    return sync_failed ? -AKAOS_EIO : 0;
}

int xscore_vfs_rename_at(uint32_t old_cwd, const char *old_path,
                         uint32_t new_cwd, const char *new_path) {
    if (!vfs_initialized || !old_path || !*old_path || !new_path || !*new_path) {
        return -AKAOS_EINVAL;
    }
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);

    char old_parent_buf[128];
    char old_name_buf[XSCORE_VFS_NAME_MAX + 1];
    if (!split_path(old_path, old_parent_buf, sizeof(old_parent_buf),
                    old_name_buf, sizeof(old_name_buf))) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EINVAL;
    }
    if ((old_name_buf[0] == '.' && old_name_buf[1] == '\0') ||
        (old_name_buf[0] == '.' && old_name_buf[1] == '.' && old_name_buf[2] == '\0')) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EINVAL;
    }

    uint32_t src_node;
    int err = xscore_vfs_resolve_path_at(old_cwd, old_path, &src_node);
    if (err != 0) {
        vfs_lock_release(lock_flags);
        return err;
    }
    if (src_node == 0) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EBUSY;
    }
    if (src_node >= node_count) {
        vfs_lock_release(lock_flags);
        return -AKAOS_ENOENT;
    }
    if (nodes[src_node].type != XSCORE_VNODE_REGULAR &&
        nodes[src_node].type != XSCORE_VNODE_DIRECTORY) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EPERM;
    }

    size_t old_len = 0;
    while (old_path[old_len]) ++old_len;
    if (old_len > 1 && old_path[old_len - 1] == '/' &&
        nodes[src_node].type != XSCORE_VNODE_DIRECTORY) {
        vfs_lock_release(lock_flags);
        return -AKAOS_ENOTDIR;
    }

    uint32_t old_parent_node = nodes[src_node].parent;
    if (old_parent_node >= XSCORE_VFS_MAX_NODES) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EINVAL;
    }
    if (!node_allows(&nodes[old_parent_node], 3)) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EACCES;
    }

    char dst_parent_buf[128];
    char dst_name_buf[XSCORE_VFS_NAME_MAX + 1];
    if (!split_path(new_path, dst_parent_buf, sizeof(dst_parent_buf),
                    dst_name_buf, sizeof(dst_name_buf))) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EINVAL;
    }
    if ((dst_name_buf[0] == '.' && dst_name_buf[1] == '\0') ||
        (dst_name_buf[0] == '.' && dst_name_buf[1] == '.' && dst_name_buf[2] == '\0')) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EINVAL;
    }

    uint32_t dst_parent_node;
    err = xscore_vfs_resolve_path_at(new_cwd, dst_parent_buf, &dst_parent_node);
    if (err != 0) {
        vfs_lock_release(lock_flags);
        return err;
    }
    if (dst_parent_node >= XSCORE_VFS_MAX_NODES ||
        nodes[dst_parent_node].type != XSCORE_VNODE_DIRECTORY) {
        vfs_lock_release(lock_flags);
        return -AKAOS_ENOTDIR;
    }
    if (!node_allows(&nodes[dst_parent_node], 3)) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EACCES;
    }

    /* Prevent moving directory into itself or its subdirectory */
    if (nodes[src_node].type == XSCORE_VNODE_DIRECTORY) {
        uint32_t cur = dst_parent_node;
        while (cur != 0 && cur < XSCORE_VFS_MAX_NODES) {
            if (cur == src_node) {
                vfs_lock_release(lock_flags);
                return -AKAOS_EINVAL;
            }
            cur = nodes[cur].parent;
        }
    }

    size_t new_len = 0;
    while (new_path[new_len]) ++new_len;
    if (new_len > 1 && new_path[new_len - 1] == '/' &&
        nodes[src_node].type != XSCORE_VNODE_DIRECTORY) {
        vfs_lock_release(lock_flags);
        return -AKAOS_ENOTDIR;
    }

    size_t dst_name_len = 0;
    while (dst_name_buf[dst_name_len]) ++dst_name_len;
    int existing = find_child(dst_parent_node, dst_name_buf, dst_name_len);
    if (existing >= 0) {
        uint32_t dst_node = (uint32_t)existing;
        if (src_node == dst_node) {
            vfs_lock_release(lock_flags);
            return 0;
        }
        if (nodes[src_node].type == XSCORE_VNODE_DIRECTORY) {
            if (nodes[dst_node].type != XSCORE_VNODE_DIRECTORY) {
                vfs_lock_release(lock_flags);
                return -AKAOS_ENOTDIR;
            }
            for (uint32_t i = 1; i < node_count; ++i) {
                if (nodes[i].type != XSCORE_VNODE_UNUSED && nodes[i].parent == dst_node) {
                    vfs_lock_release(lock_flags);
                    return -AKAOS_ENOTEMPTY;
                }
            }
        } else {
            if (nodes[dst_node].type == XSCORE_VNODE_DIRECTORY) {
                vfs_lock_release(lock_flags);
                return -AKAOS_EISDIR;
            }
        }

        if (nodes[dst_node].references != 0) {
            nodes[dst_node].unlinked = 1;
            nodes[dst_node].parent = 0;
            nodes[dst_node].name[0] = 0;
        } else {
            free_vnode_blocks(&nodes[dst_node]);
            nodes[dst_node].type = XSCORE_VNODE_UNUSED;
            nodes[dst_node].name[0] = 0;
            nodes[dst_node].size = 0;
            nodes[dst_node].data = 0;
            nodes[dst_node].uid = 0;
            nodes[dst_node].gid = 0;
            nodes[dst_node].is_dynamic = 0;
            nodes[dst_node].unlinked = 0;
        }
    }

    if (nodes[src_node].type == XSCORE_VNODE_REGULAR && !nodes[src_node].is_dynamic) {
        if (!convert_to_dynamic(&nodes[src_node])) {
            vfs_lock_release(lock_flags);
            return -AKAOS_ENOMEM;
        }
    }
    nodes[src_node].is_dynamic = 1;
    nodes[src_node].parent = dst_parent_node;
    for (size_t i = 0; i < dst_name_len; ++i) {
        nodes[src_node].name[i] = dst_name_buf[i];
    }
    nodes[src_node].name[dst_name_len] = '\0';

    int sync_needed = persistence_ready && !persistence_loading;
    vfs_lock_release(lock_flags);
    int sync_failed = sync_needed && !xscore_vfs_persistence_sync();
    return sync_failed ? -AKAOS_EIO : 0;
}

static int vfs_chmod(uint32_t cwd_node, const char *path, uint32_t target, uint32_t mode) {
    if (!vfs_initialized || (path && !*path)) return -AKAOS_EINVAL;
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    int err = path ? xscore_vfs_resolve_path_at(cwd_node, path, &target) : 0;
    if (err != 0) {
        vfs_lock_release(lock_flags);
        return err;
    }
    if (target >= XSCORE_VFS_MAX_NODES || nodes[target].type == XSCORE_VNODE_UNUSED) {
        vfs_lock_release(lock_flags);
        return -AKAOS_ENOENT;
    }
    struct xscore_vnode *node = &nodes[target];
    uint32_t caller_uid = 0;
    current_credentials(&caller_uid, 0);
    if (caller_uid != 0 && caller_uid != node->uid) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EPERM;
    }
    uint32_t type_bits = node->mode & (AKAOS_MODE_DIRECTORY |
                                       AKAOS_MODE_CHARACTER |
                                       AKAOS_MODE_REGULAR);
    node->mode = type_bits | (mode & 07777);
    if (!node->is_dynamic && node->type == XSCORE_VNODE_REGULAR) {
        if (!convert_to_dynamic(node)) {
            vfs_lock_release(lock_flags);
            return -AKAOS_ENOMEM;
        }
    }
    int sync_needed = persistence_ready && !persistence_loading;
    vfs_lock_release(lock_flags);
    if (sync_needed) (void)xscore_vfs_persistence_sync();
    return 0;
}

int xscore_vfs_chmod_at(uint32_t cwd_node, const char *path, uint32_t mode) {
    if (!path) return -AKAOS_EINVAL;
    return vfs_chmod(cwd_node, path, 0, mode);
}

int xscore_vfs_chmod_node(uint32_t target, uint32_t mode) {
    return vfs_chmod(0, 0, target, mode);
}

static int vfs_chown(uint32_t cwd_node, const char *path, uint32_t target, uint32_t uid, uint32_t gid) {
    if (!vfs_initialized || (path && !*path)) return -AKAOS_EINVAL;
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    int err = path ? xscore_vfs_resolve_path_at(cwd_node, path, &target) : 0;
    if (err != 0) {
        vfs_lock_release(lock_flags);
        return err;
    }
    if (target >= XSCORE_VFS_MAX_NODES || nodes[target].type == XSCORE_VNODE_UNUSED) {
        vfs_lock_release(lock_flags);
        return -AKAOS_ENOENT;
    }
    struct xscore_vnode *node = &nodes[target];
    uint32_t caller_uid = 0;
    current_credentials(&caller_uid, 0);
    if (caller_uid != 0) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EPERM;
    }
    if (path || uid != UINT32_MAX) node->uid = uid;
    if (path || gid != UINT32_MAX) node->gid = gid;
    if (!path) node->mode &= ~06000U;
    if (!node->is_dynamic && node->type == XSCORE_VNODE_REGULAR) {
        if (!convert_to_dynamic(node)) {
            vfs_lock_release(lock_flags);
            return -AKAOS_ENOMEM;
        }
    }
    int sync_needed = persistence_ready && !persistence_loading;
    vfs_lock_release(lock_flags);
    if (sync_needed) (void)xscore_vfs_persistence_sync();
    return 0;
}

int xscore_vfs_chown_at(uint32_t cwd_node, const char *path, uint32_t uid, uint32_t gid) {
    if (!path) return -AKAOS_EINVAL;
    return vfs_chown(cwd_node, path, 0, uid, gid);
}

int xscore_vfs_chown_node(uint32_t target, uint32_t uid, uint32_t gid) {
    return vfs_chown(0, 0, target, uid, gid);
}

int xscore_vfs_stat_at(uint32_t cwd_node, const char *path,
                       struct akaos_stat *status) {
    if (!status) return -AKAOS_EFAULT;
    uint32_t target;
    int err = xscore_vfs_resolve_path_at(cwd_node, path, &target);
    if (err != 0) return err;
    return xscore_vfs_stat_node_id(target, status);
}

int64_t xscore_vfs_open_at(struct xscore_fd_table *table, uint32_t cwd_node,
                           const char *path, uint64_t flags, uint32_t mode) {
    if (!table || !path || !*path) return -AKAOS_EINVAL;
    /* Validate before lookup, creation, or truncation. */
    if ((flags & UINT64_C(3)) > AKAOS_O_RDWR) return -AKAOS_EINVAL;
    /* Truncation requires a writable open, including for static backing. */
    if ((flags & AKAOS_O_TRUNC) && (flags & 3) == AKAOS_O_RDONLY)
        return -AKAOS_EINVAL;
    int changed = 0;
    uint32_t target;
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    int err = xscore_vfs_resolve_path_at(cwd_node, path, &target);

    /* Check exclusivity under the tree lock, before any mutation or open. */
    if (err == 0 && (flags & (AKAOS_O_CREAT | AKAOS_O_EXCL)) ==
                    (AKAOS_O_CREAT | AKAOS_O_EXCL)) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EEXIST;
    }

    if (err != 0) {
        if (flags & AKAOS_O_CREAT) {
            char parent_buf[128];
            char name_buf[XSCORE_VFS_NAME_MAX + 1];
            if (!split_path(path, parent_buf, sizeof(parent_buf), name_buf, sizeof(name_buf))) {
                vfs_lock_release(lock_flags);
                return -AKAOS_EINVAL;
            }
            uint32_t parent_node;
            int p_err = xscore_vfs_resolve_path_at(cwd_node, parent_buf, &parent_node);
            if (p_err != 0) {
                vfs_lock_release(lock_flags);
                return p_err;
            }
            if (parent_node >= XSCORE_VFS_PROC_PID_BASE ||
                (procfs_installed && (parent_node == proc_root_node || parent_node == proc_self_node))) {
                vfs_lock_release(lock_flags);
                return -AKAOS_EROFS;
            }
            if (nodes[parent_node].type != XSCORE_VNODE_DIRECTORY) {
                vfs_lock_release(lock_flags);
                return -AKAOS_ENOTDIR;
            }
            if (!node_allows(&nodes[parent_node], 3)) {
                vfs_lock_release(lock_flags);
                return -AKAOS_EACCES;
            }

            int slot = -1;
            for (uint32_t i = 0; i < XSCORE_VFS_MAX_NODES; ++i) {
                if (nodes[i].type == XSCORE_VNODE_UNUSED) {
                    slot = (int)i;
                    break;
                }
            }
            if (slot < 0) {
                vfs_lock_release(lock_flags);
                return -AKAOS_ENFILE;
            }

            struct xscore_vnode *node = &nodes[slot];
            free_vnode_blocks(node);
            node->type = XSCORE_VNODE_REGULAR;
            node->parent = parent_node;
            size_t name_len = 0;
            while (name_buf[name_len] && name_len < XSCORE_VFS_NAME_MAX) {
                node->name[name_len] = name_buf[name_len];
                ++name_len;
            }
            node->name[name_len] = 0;
            node->data = NULL;
            node->size = 0;
            current_credentials(&node->uid, &node->gid);
    node->is_dynamic = 1;
            node->references = 0;
            node->unlinked = 0;
            node->mode = AKAOS_MODE_REGULAR | (mode & 0777);
            if ((uint32_t)slot >= node_count) node_count = (uint32_t)slot + 1;
            target = (uint32_t)slot;
            changed = 1;
        } else {
            vfs_lock_release(lock_flags);
            return -AKAOS_ENOENT;
        }
    }

    if (procfs_installed) {
        if (target == proc_root_node || target == proc_self_node ||
            target == proc_net_node || target == proc_asound_node ||
            target == proc_acpi_node ||
            (target >= XSCORE_VFS_PROC_PID_BASE &&
             target < XSCORE_VFS_PROC_PID_BASE + 1000)) {
            if ((flags & 3) == AKAOS_O_WRONLY || (flags & 3) == AKAOS_O_RDWR) {
                vfs_lock_release(lock_flags);
                return -AKAOS_EISDIR;
            }
            vfs_lock_release(lock_flags);
            return xscore_fd_open_directory(table, target);
        }
    }
    /* Snapshot everything the (lock-free) descriptor-opening half needs.
     * All generated-procfs formatters and fd_open_* helpers run without
     * vfs_lock so this path never nests the tree lock under itself, never
     * holds it across formatter calls, and cannot leak it on return. */
    int node_is_directory = 0;
    int node_is_character = 0;
    /* Generated-procfs node ids can exceed XSCORE_VFS_MAX_NODES (e.g.
     * /proc/<pid>/status lives at PROC_PID_BASE + 0x1000 + pid), so this
     * test must NOT be gated on the nodes[] bounds check below. */
    int node_is_generated = procfs_installed &&
        (target == proc_self_status_node || target == proc_processes_node ||
         target == proc_mounts_node || target == proc_uptime_node || target == proc_kmsg_node ||
         target == proc_realtime_node ||
         target == proc_meminfo_node || target == proc_net_tcp_node ||
         target == proc_net_udp_node || target == proc_net_route_node ||
         target == proc_net_arp_node || target == proc_net_dev_node ||
         target == proc_cpuinfo_node || target == proc_gpuinfo_node ||
         target == proc_pci_node || target == proc_asound_cards_node ||
         target == proc_acpi_info_node || target == proc_acpi_fadt_node ||
         target == proc_acpi_madt_node ||
         (target >= XSCORE_VFS_PROC_PID_BASE + 0x1000 &&
          target < XSCORE_VFS_PROC_PID_BASE + 0x2000));
    if (target < XSCORE_VFS_MAX_NODES) {
        node_is_directory = (nodes[target].type == XSCORE_VNODE_DIRECTORY);
        node_is_character = (nodes[target].type == XSCORE_VNODE_CHARACTER);
    }
    uint64_t open_flags = flags;
    int sync_failed = 0;

    if (node_is_directory) {
        if ((flags & 3) == AKAOS_O_WRONLY || (flags & 3) == AKAOS_O_RDWR) {
            vfs_lock_release(lock_flags);
            return -AKAOS_EISDIR;
        }
        if (!node_allows(&nodes[target], 4)) {
            vfs_lock_release(lock_flags);
            return -AKAOS_EACCES;
        }
        vfs_lock_release(lock_flags);
        return xscore_fd_open_directory(table, target);
    }
    if (node_is_character) {
        vfs_lock_release(lock_flags);
        return -AKAOS_ENODEV;
    }

    /* Regular file */
    uint32_t requested = 0;
    if ((flags & 3) == AKAOS_O_RDONLY || (flags & 3) == AKAOS_O_RDWR) requested |= 4;
    if ((flags & 3) == AKAOS_O_WRONLY || (flags & 3) == AKAOS_O_RDWR) requested |= 2;
    if (!node_allows(&nodes[target], requested)) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EACCES;
    }
    if ((flags & 3) == AKAOS_O_WRONLY || (flags & 3) == AKAOS_O_RDWR) {
        if ((nodes[target].mode & AKAOS_MODE_WRITE) == 0 && !nodes[target].is_dynamic) {
            vfs_lock_release(lock_flags);
            return -AKAOS_EROFS;
        }
    }

    if (flags & AKAOS_O_TRUNC && !node_is_generated) {
        if (!nodes[target].is_dynamic) {
            nodes[target].is_dynamic = 1;
            nodes[target].data = NULL;
        }
        free_vnode_blocks(&nodes[target]);
        nodes[target].size = 0;
        changed = 1;
    }

    int sync_needed = changed && persistence_ready && !persistence_loading;
    vfs_lock_release(lock_flags);
    if (sync_needed) sync_failed = !xscore_vfs_persistence_sync();

    /* Generated procfs content: formatters do not touch the node array. */
    if (procfs_installed && node_is_generated) {
        if (target == proc_self_status_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[512];
            size_t size = xscore_process_proc_status((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_processes_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[2048];
            size_t size = xscore_process_proc_list((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_mounts_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[512];
            size_t size = xscore_vfs_format_mounts((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_kmsg_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t *generated = (uint8_t *)xscore_heap_alloc(16384);
            if (!generated) return -AKAOS_ENOMEM;
            size_t size = xscore_kernel_log_read((char *)generated, 16384, 0);
            int64_t fd = xscore_fd_open_generated(table, generated, size);
            xscore_heap_free(generated);
            return fd;
        }
        if (target == proc_uptime_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[512];
            size_t size = xscore_vfs_format_uptime((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_realtime_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[64];
            size_t size = 0;
            vfs_append_u64((char *)generated, sizeof(generated), &size,
                           xscore_time_epoch_seconds());
            vfs_append_str((char *)generated, sizeof(generated), &size, "\n");
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_meminfo_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[1024];
            size_t size = xscore_vfs_format_meminfo((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_slabinfo_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t slab_gen[2048];
            size_t size = xscore_slab_format_slabinfo((char *)slab_gen, sizeof(slab_gen));
            return xscore_fd_open_generated(table, slab_gen, size);
        }
        if (target == proc_net_tcp_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[1024];
            size_t size = xscore_socket_format_proc_tcp((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_net_udp_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[1024];
            size_t size = xscore_socket_format_proc_udp((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_net_route_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[512];
            size_t size = xscore_net_format_proc_route((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_net_arp_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[512];
            size_t size = xscore_net_format_proc_arp((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_net_dev_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint8_t generated[512];
            size_t size = xscore_net_format_proc_dev((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_cpuinfo_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t generated[2048];
            size_t size = xscore_cpu_format_proc_cpuinfo((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_gpuinfo_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t generated[2048];
            size_t size = xscore_gpu_format_proc_gpuinfo((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_pci_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t generated[4096];
            size_t size = xscore_pci_format_proc_pci((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_usbinfo_node || target == proc_bus_usb_dev_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t generated[4096];
            size_t size = xscore_usb_format_proc_devices((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_asound_cards_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t generated[1024];
            size_t size = xscore_sound_format_proc_asound((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_acpi_info_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t generated[2048];
            size_t size = xscore_acpi_format_proc_info((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_acpi_fadt_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t generated[2048];
            size_t size = xscore_acpi_format_proc_fadt((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_acpi_madt_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t generated[4096];
            size_t size = xscore_acpi_format_proc_madt((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_acpi_battery_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t generated[1024];
            size_t size = xscore_power_format_proc_battery((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target == proc_acpi_thermal_node) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            static uint8_t generated[1024];
            size_t size = xscore_power_format_proc_thermal((char *)generated, sizeof(generated));
            return xscore_fd_open_generated(table, generated, size);
        }
        if (target >= XSCORE_VFS_PROC_PID_BASE + 0x1000 &&
            target < XSCORE_VFS_PROC_PID_BASE + 0x2000) {
            if ((flags & 3) != AKAOS_O_RDONLY) return -AKAOS_EACCES;
            uint64_t pid = target - (XSCORE_VFS_PROC_PID_BASE + 0x1000);
            uint8_t generated[512];
            size_t size = xscore_process_proc_status_pid(pid, (char *)generated, sizeof(generated));
            if (size == 0) return -AKAOS_ENOENT;
            return xscore_fd_open_generated(table, generated, size);
        }
    }

    if (sync_failed) return -AKAOS_EIO;
    return xscore_fd_open_vnode(table, target, (int)open_flags);
}

int xscore_vfs_access_at(uint32_t cwd_node, const char *path, uint32_t access) {
    uint32_t target;
    int err = xscore_vfs_resolve_path_at(cwd_node, path, &target);
    if (err != 0) return err;
    return node_allows(&nodes[target], access & 7) ? 0 : -AKAOS_EACCES;
}

int xscore_vfs_read_node(uint32_t node_id, uint64_t offset, void *buffer,
                         uint64_t length, uint64_t *bytes_read) {
    if (node_id >= XSCORE_VFS_MAX_NODES) {
        return 0;
    }
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    if (nodes[node_id].type != XSCORE_VNODE_REGULAR) {
        vfs_lock_release(lock_flags);
        return 0;
    }
    struct xscore_vnode *node = &nodes[node_id];
    if (offset >= node->size) {
        if (bytes_read) *bytes_read = 0;
        vfs_lock_release(lock_flags);
        return 1;
    }
    uint64_t remaining = node->size - offset;
    if (length > remaining) length = remaining;

    uint8_t *dst = (uint8_t *)buffer;
    if (!node->is_dynamic && node->data) {
        xscore_memcpy(dst, node->data + (size_t)offset, (size_t)length);
    } else {
        uint64_t done = 0;
        while (done < length) {
            uint64_t cur_pos = offset + done;
            size_t block_idx = (size_t)(cur_pos / XSCORE_VFS_BLOCK_SIZE);
            size_t block_off = (size_t)(cur_pos % XSCORE_VFS_BLOCK_SIZE);
            size_t chunk = (size_t)(length - done);
            if (chunk > XSCORE_VFS_BLOCK_SIZE - block_off) {
                chunk = XSCORE_VFS_BLOCK_SIZE - block_off;
            }
            uint8_t *src_buf = get_vnode_block_buffer(node, block_idx, 0);
            if (src_buf) {
                xscore_memcpy(dst + done, src_buf + block_off, chunk);
            } else {
                for (size_t i = 0; i < chunk; ++i) {
                    dst[done + i] = 0;
                }
            }
            done += chunk;
        }
    }
    if (bytes_read) *bytes_read = length;
    vfs_lock_release(lock_flags);
    return 1;
}

int xscore_vfs_node_is_immutable_regular(uint32_t node_id) {
    if (node_id >= XSCORE_VFS_MAX_NODES) return 0;
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    const struct xscore_vnode *node = &nodes[node_id];
    int immutable = node->type == XSCORE_VNODE_REGULAR &&
                    !node->is_dynamic &&
                    (node->mode & AKAOS_MODE_WRITE) == 0;
    vfs_lock_release(lock_flags);
    return immutable;
}

int xscore_vfs_write_node(uint32_t node_id, uint64_t offset, const void *buffer,
                          uint64_t length, uint64_t *bytes_written) {
    if (node_id >= XSCORE_VFS_MAX_NODES) {
        return 0;
    }
    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    if (nodes[node_id].type != XSCORE_VNODE_REGULAR) {
        vfs_lock_release(lock_flags);
        return 0;
    }
    struct xscore_vnode *node = &nodes[node_id];
    if (!node->is_dynamic) {
        if (!convert_to_dynamic(node)) {
            vfs_lock_release(lock_flags);
            return 0;
        }
    }
    if (offset + length > XSCORE_VFS_FILE_MAX_SIZE) {
        if (offset >= XSCORE_VFS_FILE_MAX_SIZE) {
            vfs_lock_release(lock_flags);
            return 0;
        }
        length = XSCORE_VFS_FILE_MAX_SIZE - offset;
    }
    const uint8_t *src = (const uint8_t *)buffer;
    uint64_t written = 0;
    while (written < length) {
        uint64_t cur_pos = offset + written;
        size_t block_idx = (size_t)(cur_pos / XSCORE_VFS_BLOCK_SIZE);
        size_t block_off = (size_t)(cur_pos % XSCORE_VFS_BLOCK_SIZE);
        size_t chunk = (size_t)(length - written);
        if (chunk > XSCORE_VFS_BLOCK_SIZE - block_off) {
            chunk = XSCORE_VFS_BLOCK_SIZE - block_off;
        }
        uint8_t *dst_buf = get_vnode_block_buffer(node, block_idx, 1);
        if (!dst_buf) break;
        for (size_t i = 0; i < chunk; ++i) {
            dst_buf[block_off + i] = src[written + i];
        }
        written += chunk;
    }
    if (offset + written > node->size) {
        node->size = (size_t)(offset + written);
    }
    if (bytes_written) *bytes_written = written;
    /* Persistence walks vnode contents and calls xscore_vfs_read_node(),
     * which takes vfs_lock itself.  Drop the tree lock before syncing so a
     * normal file write cannot self-deadlock once persistence is enabled. */
    int sync_needed = persistence_ready && !persistence_loading;
    vfs_lock_release(lock_flags);
    int sync_ok = 1;
    if (sync_needed && !xscore_vfs_persistence_sync()) sync_ok = 0;
    if (!sync_ok) return 0;
    return written > 0 || length == 0;
}

int64_t xscore_vfs_truncate_node(uint32_t node_id, uint64_t length) {
    if (node_id >= XSCORE_VFS_MAX_NODES) return -AKAOS_EINVAL;
    if (length > XSCORE_VFS_FILE_MAX_SIZE) return -AKAOS_ENOSPC;

    uintptr_t lock_flags = 0;
    if (vfs_locked) lock_flags = xscore_spin_lock_irqsave(&vfs_lock);
    struct xscore_vnode *node = &nodes[node_id];
    if (node->type != XSCORE_VNODE_REGULAR) {
        vfs_lock_release(lock_flags);
        return -AKAOS_EINVAL;
    }
    if (length == node->size) {
        vfs_lock_release(lock_flags);
        return 0;
    }
    if (!node->is_dynamic && !convert_to_dynamic(node)) {
        vfs_lock_release(lock_flags);
        return -AKAOS_ENOSPC;
    }

    if (length < node->size) {
        size_t first_discarded_block = (size_t)(
            (length + XSCORE_VFS_BLOCK_SIZE - 1U) /
            XSCORE_VFS_BLOCK_SIZE);
        size_t tail_length = (size_t)(length % XSCORE_VFS_BLOCK_SIZE);
        if (tail_length != 0U && first_discarded_block != 0U) {
            uint8_t *tail = get_vnode_block_buffer(
                node, first_discarded_block - 1U, 0);
            if (tail) {
                for (size_t index = tail_length;
                     index < XSCORE_VFS_BLOCK_SIZE; ++index)
                    tail[index] = 0;
            }
        }
        for (size_t block = first_discarded_block;
             block < XSCORE_VFS_DIRECT_BLOCKS; ++block) {
            if (node->block_pages[block] == 0U) continue;
            xscore_pmm_free_page(node->block_pages[block]);
            node->block_pages[block] = 0U;
        }
    }

    node->size = (size_t)length;
    int sync_needed = persistence_ready && !persistence_loading;
    vfs_lock_release(lock_flags);
    if (sync_needed && !xscore_vfs_persistence_sync()) return -AKAOS_EIO;
    return 0;
}

int64_t xscore_vfs_getdents(struct xscore_fd_table *table, int descriptor,
                            struct akaos_dirent *entries, uint64_t capacity) {
    uint32_t directory;
    uint64_t offset;
    if (!table || !entries || capacity == 0 ||
        !xscore_fd_directory_state(table, descriptor, &directory, &offset)) {
        return -AKAOS_EBADF;
    }
    uint64_t count = 0;
    uint64_t scan = offset;

    /* Entry 0: "." */
    if (scan == 0 && count < capacity) {
        entries[count].inode = (uint64_t)directory + 1;
        entries[count].type = AKAOS_DT_DIRECTORY;
        entries[count].name[0] = '.';
        entries[count].name[1] = 0;
        ++count;
        ++scan;
    }

    /* Entry 1: ".." */
    if (scan == 1 && count < capacity) {
        uint64_t parent_inode;
        if (procfs_installed &&
            directory >= XSCORE_VFS_PROC_PID_BASE &&
            directory < XSCORE_VFS_PROC_PID_BASE + 1000) {
            parent_inode = (uint64_t)proc_root_node + 1;
        } else if (directory < XSCORE_VFS_MAX_NODES) {
            parent_inode = (uint64_t)nodes[directory].parent + 1;
        } else {
            parent_inode = 1;
        }
        entries[count].inode = parent_inode;
        entries[count].type = AKAOS_DT_DIRECTORY;
        entries[count].name[0] = '.';
        entries[count].name[1] = '.';
        entries[count].name[2] = 0;
        ++count;
        ++scan;
    }

    if (procfs_installed &&
        directory >= XSCORE_VFS_PROC_PID_BASE &&
        directory < XSCORE_VFS_PROC_PID_BASE + 1000) {
        uint64_t pid = directory - XSCORE_VFS_PROC_PID_BASE;
        if (scan == 2 && count < capacity) {
            entries[count].inode = (uint64_t)XSCORE_VFS_PROC_PID_BASE + 0x1000 + pid;
            entries[count].type = AKAOS_DT_REGULAR;
            const char sname[] = "status";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
    } else if (procfs_installed && directory == proc_root_node) {
        /* scan == 2: self */
        if (scan == 2 && count < capacity) {
            entries[count].inode = (uint64_t)proc_self_node + 1;
            entries[count].type = AKAOS_DT_DIRECTORY;
            const char sname[] = "self";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan == 3: processes */
        if (scan == 3 && count < capacity) {
            entries[count].inode = (uint64_t)proc_processes_node + 1;
            entries[count].type = AKAOS_DT_REGULAR;
            const char sname[] = "processes";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan == 4: mounts */
        if (scan == 4 && count < capacity) {
            entries[count].inode = (uint64_t)proc_mounts_node + 1;
            entries[count].type = AKAOS_DT_REGULAR;
            const char sname[] = "mounts";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan == 5: uptime */
        if (scan == 5 && count < capacity) {
            entries[count].inode = (uint64_t)proc_uptime_node + 1;
            entries[count].type = AKAOS_DT_REGULAR;
            const char sname[] = "uptime";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan == 6: meminfo */
        if (scan == 6 && count < capacity) {
            entries[count].inode = (uint64_t)proc_meminfo_node + 1;
            entries[count].type = AKAOS_DT_REGULAR;
            const char sname[] = "meminfo";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan == 7: net */
        if (scan == 7 && count < capacity) {
            entries[count].inode = (uint64_t)proc_net_node + 1;
            entries[count].type = AKAOS_DT_DIRECTORY;
            const char sname[] = "net";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan == 8: cpuinfo */
        if (scan == 8 && count < capacity) {
            entries[count].inode = (uint64_t)proc_cpuinfo_node + 1;
            entries[count].type = AKAOS_DT_REGULAR;
            const char sname[] = "cpuinfo";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan == 9: gpuinfo */
        if (scan == 9 && count < capacity) {
            entries[count].inode = (uint64_t)proc_gpuinfo_node + 1;
            entries[count].type = AKAOS_DT_REGULAR;
            const char sname[] = "gpuinfo";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan == 10: pci */
        if (scan == 10 && count < capacity) {
            entries[count].inode = (uint64_t)proc_pci_node + 1;
            entries[count].type = AKAOS_DT_REGULAR;
            const char sname[] = "pci";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan == 11: asound */
        if (scan == 11 && count < capacity) {
            entries[count].inode = (uint64_t)proc_asound_node + 1;
            entries[count].type = AKAOS_DT_DIRECTORY;
            const char sname[] = "asound";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan == 12: acpi */
        if (scan == 12 && count < capacity) {
            entries[count].inode = (uint64_t)proc_acpi_node + 1;
            entries[count].type = AKAOS_DT_DIRECTORY;
            const char sname[] = "acpi";
            for (size_t i = 0; i < sizeof(sname); ++i) entries[count].name[i] = sname[i];
            ++count;
            ++scan;
        }
        /* scan >= 13: active PIDs */
        uint64_t active_pids[16];
        size_t total_pids = xscore_process_get_active_pids(active_pids, 16);
        while (count < capacity && (scan - 13) < total_pids) {
            uint64_t pid = active_pids[scan - 13];
            entries[count].inode = (uint64_t)XSCORE_VFS_PROC_PID_BASE + pid;
            entries[count].type = AKAOS_DT_DIRECTORY;
            char digits[20];
            size_t dcount = 0;
            if (pid == 0) digits[dcount++] = '0';
            while (pid > 0 && dcount < sizeof(digits)) {
                digits[dcount++] = (char)('0' + (pid % 10));
                pid /= 10;
            }
            size_t n = 0;
            while (dcount > 0 && n < AKAOS_NAME_MAX) {
                entries[count].name[n++] = digits[--dcount];
            }
            entries[count].name[n] = '\0';
            ++count;
            ++scan;
        }
    } else {
        /* Child nodes start at scan >= 2, corresponding to node_idx = scan - 1 (skipping root node 0) */
        while (scan - 1 < node_count && count < capacity) {
            uint32_t node_idx = (uint32_t)(scan - 1);
            const struct xscore_vnode *node = &nodes[node_idx];
            ++scan;
            if (node->type == XSCORE_VNODE_UNUSED || node->parent != directory ||
                node_idx == directory || node->name[0] == '\0') {
                continue;
            }
            entries[count].inode = (uint64_t)node_idx + 1;
            entries[count].type = node->type == XSCORE_VNODE_DIRECTORY
                                      ? AKAOS_DT_DIRECTORY
                                      : (node->type == XSCORE_VNODE_CHARACTER
                                             ? AKAOS_DT_CHARACTER
                                             : AKAOS_DT_REGULAR);
            uint32_t name_index = 0;
            while (node->name[name_index] && name_index < AKAOS_NAME_MAX) {
                entries[count].name[name_index] = node->name[name_index];
                ++name_index;
            }
            entries[count].name[name_index] = 0;
            ++count;
        }
    }

    if (!xscore_fd_directory_set_offset(table, descriptor, scan)) {
        return -AKAOS_EBADF;
    }
    return (int64_t)count;
}

int64_t xscore_vfs_open(struct xscore_fd_table *table, const char *path,
                        uint64_t flags) {
    return xscore_vfs_open_at(table, 0, path, flags, 0);
}

int xscore_vfs_stat(const char *path, struct akaos_stat *status) {
    return xscore_vfs_stat_at(0, path, status) == 0;
}

int xscore_vfs_stat_node_id(uint32_t node_id, struct akaos_stat *status) {
    if (!status) return -AKAOS_EBADF;
    if (procfs_installed) {
        if (node_id >= XSCORE_VFS_PROC_PID_BASE &&
            node_id < XSCORE_VFS_PROC_PID_BASE + 1000) {
            uint64_t pid = node_id - XSCORE_VFS_PROC_PID_BASE;
            uint32_t uid = 0, gid = 0;
            enum xscore_process_state st;
            if (!xscore_process_pid_info(pid, &uid, &gid, &st)) return -AKAOS_ENOENT;
            status->inode = (uint64_t)node_id + 1;
            status->size = 0;
            status->mode = AKAOS_MODE_DIRECTORY | 0555;
            status->type = AKAOS_DT_DIRECTORY;
            status->uid = uid;
            status->gid = gid;
            return 0;
        }
        if (node_id >= XSCORE_VFS_PROC_PID_BASE + 0x1000 &&
            node_id < XSCORE_VFS_PROC_PID_BASE + 0x2000) {
            uint64_t pid = node_id - (XSCORE_VFS_PROC_PID_BASE + 0x1000);
            uint32_t uid = 0, gid = 0;
            enum xscore_process_state st;
            if (!xscore_process_pid_info(pid, &uid, &gid, &st)) return -AKAOS_ENOENT;
            status->inode = (uint64_t)node_id + 1;
            status->size = 512;
            status->mode = AKAOS_MODE_REGULAR | 0444;
            status->type = AKAOS_DT_REGULAR;
            status->uid = uid;
            status->gid = gid;
            return 0;
        }
    }
    if (node_id >= XSCORE_VFS_MAX_NODES || nodes[node_id].type == XSCORE_VNODE_UNUSED) {
        return -AKAOS_EBADF;
    }
    const struct xscore_vnode *node = &nodes[node_id];
    status->inode = (uint64_t)node_id + 1;
    status->size = node->size;
    status->mode = node->mode;
    status->type = node->type == XSCORE_VNODE_DIRECTORY
                       ? AKAOS_DT_DIRECTORY
                       : (node->type == XSCORE_VNODE_CHARACTER
                              ? AKAOS_DT_CHARACTER
                              : AKAOS_DT_REGULAR);
    status->uid = node->uid;
    status->gid = node->gid;
    return 0;
}

uint32_t xscore_vfs_node_count(void) {
    return node_count;
}

static size_t vfs_strlen(const char *s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

size_t xscore_vfs_format_mounts(char *buffer, size_t capacity) {
    if (!buffer || capacity == 0) return 0;
    init_mount_table_defaults();
    size_t written = 0;
    for (size_t i = 0; i < XSCORE_VFS_MAX_MOUNTS; ++i) {
        if (!mount_table[i].active) continue;
        const char *src = mount_table[i].source[0] ? mount_table[i].source : "none";
        const char *tgt = mount_table[i].target[0] ? mount_table[i].target : "/";
        const char *fst = mount_table[i].fstype[0] ? mount_table[i].fstype : "akafs";
        const char *opt = (mount_table[i].flags & AKAOS_MS_RDONLY) ? "ro" : "rw";

        size_t slen = vfs_strlen(src);
        size_t tlen = vfs_strlen(tgt);
        size_t flen = vfs_strlen(fst);
        size_t olen = vfs_strlen(opt);

        if (written + slen + tlen + flen + olen + 10 >= capacity) break;

        for (size_t j = 0; j < slen; ++j) buffer[written++] = src[j];
        buffer[written++] = ' ';
        for (size_t j = 0; j < tlen; ++j) buffer[written++] = tgt[j];
        buffer[written++] = ' ';
        for (size_t j = 0; j < flen; ++j) buffer[written++] = fst[j];
        buffer[written++] = ' ';
        for (size_t j = 0; j < olen; ++j) buffer[written++] = opt[j];
        buffer[written++] = ' ';
        buffer[written++] = '0';
        buffer[written++] = ' ';
        buffer[written++] = '0';
        buffer[written++] = '\n';
    }
    if (written < capacity) buffer[written] = '\0';
    return written;
}

static void vfs_append_str(char *buffer, size_t capacity, size_t *used, const char *s) {
    if (!buffer || !used || !s) return;
    while (*s && *used + 1 < capacity) {
        buffer[(*used)++] = *s++;
    }
    buffer[*used < capacity ? *used : capacity - 1] = '\0';
}

static void vfs_append_u64(char *buffer, size_t capacity, size_t *used, uint64_t val) {
    char digits[24];
    size_t count = 0;
    if (val == 0) digits[count++] = '0';
    while (val > 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (count > 0 && *used + 1 < capacity) {
        buffer[(*used)++] = digits[--count];
    }
    buffer[*used < capacity ? *used : capacity - 1] = '\0';
}

static uint64_t u64_div_mod(uint64_t num, uint64_t den, uint64_t *rem) {
    if (den == 0) {
        if (rem) *rem = 0;
        return 0;
    }
    uint64_t quot = 0;
    uint64_t cur = 0;
    for (int i = 63; i >= 0; --i) {
        cur = (cur << 1) | ((num >> i) & 1);
        if (cur >= den) {
            cur -= den;
            quot |= ((uint64_t)1 << i);
        }
    }
    if (rem) *rem = cur;
    return quot;
}

size_t xscore_vfs_format_uptime(char *buffer, size_t capacity) {
    if (!buffer || capacity == 0) return 0;
    uint64_t ticks = xscore_timer_ticks();
    uint32_t freq = xscore_timer_frequency();
    if (freq == 0) freq = 100;
    uint64_t rem = 0;
    uint64_t sec = u64_div_mod(ticks, (uint64_t)freq, &rem);
    uint64_t frac = u64_div_mod(rem * 100, (uint64_t)freq, NULL);

    size_t used = 0;
    vfs_append_u64(buffer, capacity, &used, sec);
    vfs_append_str(buffer, capacity, &used, ".");
    if (frac < 10) vfs_append_str(buffer, capacity, &used, "0");
    vfs_append_u64(buffer, capacity, &used, frac);
    vfs_append_str(buffer, capacity, &used, " ");
    vfs_append_u64(buffer, capacity, &used, sec);
    vfs_append_str(buffer, capacity, &used, ".");
    if (frac < 10) vfs_append_str(buffer, capacity, &used, "0");
    vfs_append_u64(buffer, capacity, &used, frac);
    vfs_append_str(buffer, capacity, &used, "\n");
    return used;
}

size_t xscore_vfs_format_meminfo(char *buffer, size_t capacity) {
    if (!buffer || capacity == 0) return 0;
    uint64_t total_pages = xscore_pmm_total_page_count();
    uint64_t free_pages = xscore_pmm_free_page_count();
    uint64_t total_kb = total_pages * 4;
    uint64_t free_kb = free_pages * 4;
    uint64_t slab_kb = xscore_slab_get_total_allocated_bytes() / 1024;
    uint64_t slab_active_kb = xscore_slab_get_active_bytes() / 1024;

    size_t used = 0;
    vfs_append_str(buffer, capacity, &used, "MemTotal:       ");
    vfs_append_u64(buffer, capacity, &used, total_kb);
    vfs_append_str(buffer, capacity, &used, " kB\nMemFree:        ");
    vfs_append_u64(buffer, capacity, &used, free_kb);
    vfs_append_str(buffer, capacity, &used, " kB\nMemAvailable:   ");
    vfs_append_u64(buffer, capacity, &used, free_kb);
    vfs_append_str(buffer, capacity, &used, " kB\nBuffers:               0 kB\nCached:            16384 kB\nSwapTotal:             0 kB\nSwapFree:              0 kB\nSlab:           ");
    vfs_append_u64(buffer, capacity, &used, slab_kb ? slab_kb : 512);
    vfs_append_str(buffer, capacity, &used, " kB\nSReclaimable:   ");
    vfs_append_u64(buffer, capacity, &used, slab_active_kb ? slab_active_kb / 2 : 256);
    vfs_append_str(buffer, capacity, &used, " kB\nSUnreclaim:     ");
    vfs_append_u64(buffer, capacity, &used, slab_active_kb ? slab_active_kb / 2 : 256);
    vfs_append_str(buffer, capacity, &used, " kB\nPageTables:          512 kB\nHugePages_Total:       0\nHugePages_Free:        0\nHugepagesize:       2048 kB\n");
    return used;
}

int xscore_vfs_mount(const char *source, const char *target, const char *fstype, uint64_t flags) {
    if (!vfs_initialized || !target || !*target) return -AKAOS_EINVAL;
    init_mount_table_defaults();

    uint32_t caller_uid = 0;
    current_credentials(&caller_uid, 0);
    if (caller_uid != 0) return -AKAOS_EPERM;

    uint32_t target_node = 0;
    int err = xscore_vfs_resolve_path_at(0, target, &target_node);
    if (err != 0) return err;
    if (nodes[target_node].type != XSCORE_VNODE_DIRECTORY) return -AKAOS_ENOTDIR;

    /* Check if already mounted */
    int slot = -1;
    for (size_t i = 0; i < XSCORE_VFS_MAX_MOUNTS; ++i) {
        if (mount_table[i].active) {
            int match = 1;
            size_t j = 0;
            while (target[j] || mount_table[i].target[j]) {
                if (target[j] != mount_table[i].target[j]) { match = 0; break; }
                j++;
            }
            if (match) {
                if (flags & AKAOS_MS_REMOUNT) {
                    mount_table[i].flags = flags;
                    return 0;
                }
                return -AKAOS_EBUSY;
            }
        } else if (slot < 0) {
            slot = (int)i;
        }
    }

    if (slot < 0) return -AKAOS_ENOMEM;

    mount_table[slot].active = 1;
    mount_table[slot].flags = flags;
    mount_table[slot].target_node = target_node;
    vfs_copy_str(mount_table[slot].source, sizeof(mount_table[slot].source), source ? source : "none");
    vfs_copy_str(mount_table[slot].target, sizeof(mount_table[slot].target), target);
    vfs_copy_str(mount_table[slot].fstype, sizeof(mount_table[slot].fstype), fstype ? fstype : "akafs");
    return 0;
}

int xscore_vfs_umount(const char *target, uint64_t flags) {
    (void)flags;
    if (!vfs_initialized || !target || !*target) return -AKAOS_EINVAL;
    init_mount_table_defaults();

    uint32_t caller_uid = 0;
    current_credentials(&caller_uid, 0);
    if (caller_uid != 0) return -AKAOS_EPERM;

    /* Cannot unmount root */
    if (target[0] == '/' && target[1] == '\0') return -AKAOS_EBUSY;

    for (size_t i = 0; i < XSCORE_VFS_MAX_MOUNTS; ++i) {
        if (!mount_table[i].active) continue;
        int match = 1;
        size_t j = 0;
        while (target[j] || mount_table[i].target[j]) {
            if (target[j] != mount_table[i].target[j]) { match = 0; break; }
            j++;
        }
        if (match) {
            mount_table[i].active = 0;
            mount_table[i].source[0] = '\0';
            mount_table[i].target[0] = '\0';
            mount_table[i].fstype[0] = '\0';
            mount_table[i].flags = 0;
            return 0;
        }
    }
    return -AKAOS_EINVAL;
}

static void add_autocomplete_match(char matches[][32], size_t max_matches,
                                   size_t *count, const char *name) {
    if (!name || !*name || *count >= max_matches) return;
    if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) return;
    for (size_t i = 0; i < *count; ++i) {
        int same = 1;
        size_t j = 0;
        while (matches[i][j] || name[j]) {
            if (matches[i][j] != name[j]) { same = 0; break; }
            j++;
        }
        if (same) return;
    }
    size_t k = 0;
    while (name[k] && k < 31) {
        matches[*count][k] = name[k];
        k++;
    }
    matches[*count][k] = '\0';
    (*count)++;
}

int xscore_vfs_autocomplete(uint32_t cwd_node, const char *word, int is_command,
                            char *completion, size_t max_completion,
                            int *is_dir, char matches[][32], size_t max_matches,
                            size_t *out_match_count) {
    if (!completion || max_completion == 0 || !matches || !out_match_count) return 0;
    *out_match_count = 0;
    completion[0] = '\0';
    if (is_dir) *is_dir = 0;
    if (!word) word = "";

    size_t word_len = 0;
    while (word[word_len]) word_len++;

    int has_slash = 0;
    size_t last_slash = 0;
    for (size_t i = 0; i < word_len; ++i) {
        if (word[i] == '/') {
            has_slash = 1;
            last_slash = i;
        }
    }

    const char *prefix = word;
    size_t prefix_len = word_len;
    uint32_t search_dir_node = cwd_node;

    if (has_slash) {
        char dir_part[XSCORE_VFS_PATH_MAX + 1];
        if (last_slash == 0) {
            dir_part[0] = '/';
            dir_part[1] = '\0';
        } else {
            size_t cp_len = last_slash < sizeof(dir_part) - 1 ? last_slash : sizeof(dir_part) - 1;
            for (size_t i = 0; i < cp_len; ++i) dir_part[i] = word[i];
            dir_part[cp_len] = '\0';
        }
        prefix = &word[last_slash + 1];
        prefix_len = 0;
        while (prefix[prefix_len]) prefix_len++;

        if (xscore_vfs_resolve_path_at(cwd_node, dir_part, &search_dir_node) != 0) {
            return 0;
        }
    } else if (is_command) {
        static const char *builtins[] = {
            "cd", "pwd", "echo", "exit", "clear", "export", "unset", "history", "help"
        };
        for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); ++i) {
            int match = 1;
            for (size_t j = 0; j < prefix_len; ++j) {
                if (builtins[i][j] != prefix[j]) { match = 0; break; }
            }
            if (match) {
                add_autocomplete_match(matches, max_matches, out_match_count, builtins[i]);
            }
        }

        uint32_t bin_node = 0;
        if (xscore_vfs_resolve_path_at(0, "/bin", &bin_node) == 0) {
            search_dir_node = bin_node;
        } else {
            search_dir_node = 0;
        }
    }

    if (search_dir_node < node_count) {
        for (uint32_t i = 1; i < node_count && *out_match_count < max_matches; ++i) {
            if (nodes[i].type == XSCORE_VNODE_UNUSED) continue;
            if (nodes[i].parent != search_dir_node) continue;
            if (nodes[i].name[0] == '\0') continue;

            int match = 1;
            for (size_t j = 0; j < prefix_len; ++j) {
                if (nodes[i].name[j] != prefix[j]) { match = 0; break; }
            }
            if (match) {
                add_autocomplete_match(matches, max_matches, out_match_count, nodes[i].name);
            }
        }
    }

    /* Fallback: if not command and no slash, also check /bin if nothing found in CWD */
    if (!has_slash && !is_command && *out_match_count == 0) {
        uint32_t bin_node = 0;
        if (xscore_vfs_resolve_path_at(0, "/bin", &bin_node) == 0) {
            for (uint32_t i = 1; i < node_count && *out_match_count < max_matches; ++i) {
                if (nodes[i].type == XSCORE_VNODE_UNUSED) continue;
                if (nodes[i].parent != bin_node) continue;
                int match = 1;
                for (size_t j = 0; j < prefix_len; ++j) {
                    if (nodes[i].name[j] != prefix[j]) { match = 0; break; }
                }
                if (match) {
                    add_autocomplete_match(matches, max_matches, out_match_count, nodes[i].name);
                }
            }
        }
    }

    if (*out_match_count == 0) return 0;

    if (*out_match_count == 1) {
        const char *match_name = matches[0];
        size_t mlen = 0;
        while (match_name[mlen]) mlen++;

        size_t c_idx = 0;
        for (size_t i = prefix_len; i < mlen && c_idx + 2 < max_completion; ++i) {
            completion[c_idx++] = match_name[i];
        }

        int child_idx = find_child(search_dir_node, match_name, mlen);
        int check_dir = 0;
        if (child_idx >= 0 && (uint32_t)child_idx < node_count) {
            if (nodes[child_idx].type == XSCORE_VNODE_DIRECTORY) check_dir = 1;
        }

        if (check_dir) {
            if (c_idx + 1 < max_completion) completion[c_idx++] = '/';
            if (is_dir) *is_dir = 1;
        } else {
            if (c_idx + 1 < max_completion) completion[c_idx++] = ' ';
            if (is_dir) *is_dir = 0;
        }
        completion[c_idx] = '\0';
        return 1;
    }

    /* Multiple matches: compute longest common prefix */
    size_t common_len = 0;
    for (;;) {
        char ch = matches[0][common_len];
        if (!ch) break;
        int all_same = 1;
        for (size_t m = 1; m < *out_match_count; ++m) {
            if (matches[m][common_len] != ch) { all_same = 0; break; }
        }
        if (!all_same) break;
        common_len++;
    }

    if (common_len > prefix_len) {
        size_t c_idx = 0;
        for (size_t i = prefix_len; i < common_len && c_idx + 1 < max_completion; ++i) {
            completion[c_idx++] = matches[0][i];
        }
        completion[c_idx] = '\0';
    } else {
        completion[0] = '\0';
    }

    return (int)*out_match_count;
}
