/* ============================================================
 * akaOS — In-Memory Filesystem Implementation
 * ============================================================
 * A simple RAM-based directory tree with path resolution,
 * file creation, and basic content read/write.
 */

#include "fs.h"
#include "string.h"

/* Node storage pool */
static fs_node_t nodes[FS_MAX_NODES];
static fs_node_t *root = 0;
static fs_node_t *cwd  = 0;

/* Allocate a new node from the pool */
static fs_node_t *alloc_node(void) {
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (!nodes[i].used) {
            memset(&nodes[i], 0, sizeof(fs_node_t));
            nodes[i].used = 1;
            return &nodes[i];
        }
    }
    return 0; /* Out of nodes */
}

/* Create a node as a child of parent */
static fs_node_t *create_child(fs_node_t *parent, const char *name, int type) {
    if (!parent || parent->type != FS_DIRECTORY)
        return 0;
    if (parent->child_count >= FS_MAX_CHILDREN)
        return 0;

    /* Check if name already exists */
    for (int i = 0; i < parent->child_count; i++) {
        if (strcmp(parent->children[i]->name, name) == 0)
            return 0;
    }

    fs_node_t *node = alloc_node();
    if (!node) return 0;

    strncpy(node->name, name, FS_MAX_NAME - 1);
    node->type = type;
    node->parent = parent;
    parent->children[parent->child_count++] = node;

    return node;
}

/* Resolve a path component from a starting node */
static fs_node_t *resolve_from(fs_node_t *start, const char *path) {
    if (!start || !path) return 0;

    fs_node_t *current = start;
    char buf[FS_MAX_PATH];
    strncpy(buf, path, FS_MAX_PATH - 1);
    buf[FS_MAX_PATH - 1] = '\0';

    char *token = buf;
    while (*token) {
        /* Skip leading slashes */
        while (*token == '/') token++;
        if (*token == '\0') break;

        /* Find end of this component */
        char *end = token;
        while (*end && *end != '/') end++;
        char saved = *end;
        *end = '\0';

        if (strlen(token) == 0) {
            token = end + (saved ? 1 : 0);
            continue;
        }

        if (strcmp(token, ".") == 0) {
            /* Current directory — no change */
        } else if (strcmp(token, "..") == 0) {
            /* Parent directory */
            if (current->parent)
                current = current->parent;
        } else {
            /* Find child by name */
            fs_node_t *found = 0;
            for (int i = 0; i < current->child_count; i++) {
                if (strcmp(current->children[i]->name, token) == 0) {
                    found = current->children[i];
                    break;
                }
            }
            if (!found) return 0;
            current = found;
        }

        if (saved)
            token = end + 1;
        else
            break;
    }

    return current;
}

/* Split a path into parent path and basename.
 * Writes parent path into parent_buf and returns pointer to basename. */
static const char *split_path(const char *path, char *parent_buf, int buf_size) {
    strncpy(parent_buf, path, buf_size - 1);
    parent_buf[buf_size - 1] = '\0';

    /* Find last slash */
    int last_slash = -1;
    for (int i = 0; parent_buf[i]; i++) {
        if (parent_buf[i] == '/')
            last_slash = i;
    }

    if (last_slash < 0) {
        /* No slash — parent is cwd, basename is the whole path */
        parent_buf[0] = '\0';
        return path;
    }

    if (last_slash == 0) {
        /* Root slash — parent is "/" */
        parent_buf[0] = '/';
        parent_buf[1] = '\0';
    } else {
        parent_buf[last_slash] = '\0';
    }

    return path + last_slash + 1;
}

/* ============================================================
 * Public API
 * ============================================================ */

void fs_init(void) {
    memset(nodes, 0, sizeof(nodes));

    /* Create root directory */
    root = alloc_node();
    strcpy(root->name, "/");
    root->type = FS_DIRECTORY;
    root->parent = root; /* Root's parent is itself */

    /* Create default Unix directory structure */
    fs_node_t *home = create_child(root, "home", FS_DIRECTORY);
    fs_node_t *home_root = create_child(home, "root", FS_DIRECTORY);
    (void)home_root;

    fs_node_t *etc = create_child(root, "etc", FS_DIRECTORY);

    /* /etc/hostname */
    fs_node_t *hostname = create_child(etc, "hostname", FS_FILE);
    if (hostname) {
        strcpy(hostname->content, "akaOS\n");
        hostname->size = 6;
    }

    /* /etc/motd */
    fs_node_t *motd = create_child(etc, "motd", FS_FILE);
    if (motd) {
        const char *msg = "Welcome to akaOS — a simple Unix-like operating system.\n";
        strcpy(motd->content, msg);
        motd->size = strlen(msg);
    }

    /* /etc/os-release */
    fs_node_t *osrel = create_child(etc, "os-release", FS_FILE);
    if (osrel) {
        const char *data = "NAME=\"akaOS\"\nVERSION=\"1.1\"\nARCH=\"x86_64\"\n";
        strcpy(osrel->content, data);
        osrel->size = strlen(data);
    }

    create_child(root, "tmp", FS_DIRECTORY);
    create_child(root, "var", FS_DIRECTORY);
    create_child(root, "bin", FS_DIRECTORY);
    create_child(root, "dev", FS_DIRECTORY);

    /* Set CWD to /home/root */
    cwd = resolve_from(root, "home/root");
    if (!cwd) cwd = root;
}

fs_node_t *fs_get_root(void) { return root; }
fs_node_t *fs_get_cwd(void)  { return cwd; }
void fs_set_cwd(fs_node_t *dir) { if (dir && dir->type == FS_DIRECTORY) cwd = dir; }

fs_node_t *fs_resolve_path(const char *path) {
    if (!path || !path[0]) return cwd;

    if (path[0] == '/')
        return resolve_from(root, path + 1);
    else
        return resolve_from(cwd, path);
}

fs_node_t *fs_create_file(const char *path) {
    char parent_path[FS_MAX_PATH];
    const char *name = split_path(path, parent_path, FS_MAX_PATH);

    fs_node_t *parent;
    if (parent_path[0] == '\0')
        parent = cwd;
    else
        parent = fs_resolve_path(parent_path);

    if (!parent || parent->type != FS_DIRECTORY)
        return 0;

    return create_child(parent, name, FS_FILE);
}

fs_node_t *fs_create_dir(const char *path) {
    char parent_path[FS_MAX_PATH];
    const char *name = split_path(path, parent_path, FS_MAX_PATH);

    fs_node_t *parent;
    if (parent_path[0] == '\0')
        parent = cwd;
    else
        parent = fs_resolve_path(parent_path);

    if (!parent || parent->type != FS_DIRECTORY)
        return 0;

    return create_child(parent, name, FS_DIRECTORY);
}

int fs_remove(const char *path) {
    fs_node_t *node = fs_resolve_path(path);
    if (!node || node == root) return -1;

    /* Can't remove non-empty directories */
    if (node->type == FS_DIRECTORY && node->child_count > 0)
        return -1;

    /* Remove from parent's children list */
    fs_node_t *parent = node->parent;
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == node) {
            /* Shift remaining children */
            for (int j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->child_count--;
            break;
        }
    }

    node->used = 0;
    return 0;
}

void fs_get_path(fs_node_t *node, char *buf, int buf_size) {
    if (!node || !buf || buf_size < 2) return;

    if (node == root) {
        buf[0] = '/';
        buf[1] = '\0';
        return;
    }

    /* Build path by walking up to root */
    char parts[16][FS_MAX_NAME];
    int depth = 0;

    fs_node_t *n = node;
    while (n && n != root && depth < 16) {
        strncpy(parts[depth], n->name, FS_MAX_NAME - 1);
        parts[depth][FS_MAX_NAME - 1] = '\0';
        depth++;
        n = n->parent;
    }

    /* Construct path string */
    buf[0] = '\0';
    int pos = 0;

    for (int i = depth - 1; i >= 0; i--) {
        if (pos < buf_size - 1) buf[pos++] = '/';
        int len = strlen(parts[i]);
        for (int j = 0; j < len && pos < buf_size - 1; j++)
            buf[pos++] = parts[i][j];
    }

    if (pos == 0 && buf_size > 1) buf[pos++] = '/';
    buf[pos] = '\0';
}

int fs_write(fs_node_t *file, const char *data, int len) {
    if (!file || file->type != FS_FILE) return -1;
    if (len > FS_MAX_CONTENT - 1) len = FS_MAX_CONTENT - 1;

    memcpy(file->content, data, len);
    file->content[len] = '\0';
    file->size = len;
    return 0;
}

int fs_append(fs_node_t *file, const char *data, int len) {
    if (!file || file->type != FS_FILE) return -1;
    if (file->size + len > FS_MAX_CONTENT - 1)
        len = FS_MAX_CONTENT - 1 - file->size;
    if (len <= 0) return -1;

    memcpy(file->content + file->size, data, len);
    file->size += len;
    file->content[file->size] = '\0';
    return 0;
}

int fs_get_node_count(void) {
    int count = 0;
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].used) count++;
    }
    return count;
}
