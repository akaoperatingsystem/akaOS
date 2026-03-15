/* ============================================================
 * akaOS — In-Memory Filesystem Header
 * ============================================================
 * Simple tree-based VFS with directories and files stored in RAM.
 */

#ifndef FS_H
#define FS_H

#include <stdint.h>
#include <stddef.h>

#define FS_FILE         0
#define FS_DIRECTORY    1

#define FS_MAX_NAME     64
#define FS_MAX_CONTENT  1024
#define FS_MAX_CHILDREN 16
#define FS_MAX_NODES    64
#define FS_MAX_PATH     256

typedef struct fs_node {
    char    name[FS_MAX_NAME];
    int     type;                               /* FS_FILE or FS_DIRECTORY */
    char    content[FS_MAX_CONTENT];            /* File content */
    int     size;                                /* Content size in bytes */
    struct  fs_node *children[FS_MAX_CHILDREN]; /* Directory children */
    int     child_count;
    struct  fs_node *parent;
    int     used;                                /* Is this node allocated? */
} fs_node_t;

/* Initialize the filesystem with default directory structure */
void fs_init(void);

/* Get the root node */
fs_node_t *fs_get_root(void);

/* Get the current working directory */
fs_node_t *fs_get_cwd(void);

/* Set the current working directory */
void fs_set_cwd(fs_node_t *dir);

/* Resolve a path (absolute or relative) to a node. Returns NULL if not found. */
fs_node_t *fs_resolve_path(const char *path);

/* Create a file at the given path. Returns the new node or NULL on error. */
fs_node_t *fs_create_file(const char *path);

/* Create a directory at the given path. Returns the new node or NULL on error. */
fs_node_t *fs_create_dir(const char *path);

/* Remove a node (file or empty directory). Returns 0 on success. */
int fs_remove(const char *path);

/* Get the full path string for a node (writes into buf). */
void fs_get_path(fs_node_t *node, char *buf, int buf_size);

/* Write content to a file (overwrite) */
int fs_write(fs_node_t *file, const char *data, int len);

/* Append content to a file */
int fs_append(fs_node_t *file, const char *data, int len);

/* Get the total number of allocated filesystem nodes */
int fs_get_node_count(void);

#endif /* FS_H */
