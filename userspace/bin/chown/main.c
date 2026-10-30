#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static void write_str(int fd, const char *s) {
    if (!s) return;
    size_t len = strlen(s);
    size_t written = 0;
    while (written < len) {
        ssize_t res = write(fd, s + written, len - written);
        if (res <= 0) break;
        written += (size_t)res;
    }
}

static void print_usage(void) {
    write_str(STDOUT_FILENO, "Usage: chown [OPTION]... [OWNER][:[GROUP]] FILE...\n");
    write_str(STDOUT_FILENO, "Change the owner and/or group of each FILE to OWNER and/or GROUP.\n\n");
    write_str(STDOUT_FILENO, "  -h, --help     display this help and exit\n\n");
    write_str(STDOUT_FILENO, "OWNER and GROUP may be numeric IDs or names ('root' = 0).\n");
}

static int parse_id(const char *str, uint32_t *out_id) {
    if (!str || !*str) return 0;
    if (strcmp(str, "root") == 0) {
        if (out_id) *out_id = 0;
        return 1;
    }
    if (strcmp(str, "daemon") == 0) {
        if (out_id) *out_id = 1;
        return 1;
    }
    if (strcmp(str, "user") == 0) {
        if (out_id) *out_id = 1000;
        return 1;
    }
    uint32_t val = 0;
    for (size_t i = 0; str[i]; ++i) {
        if (str[i] < '0' || str[i] > '9') return 0;
        val = val * 10 + (uint32_t)(str[i] - '0');
    }
    if (out_id) *out_id = val;
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        write_str(STDERR_FILENO, "chown: missing operand\n");
        write_str(STDERR_FILENO, "Try 'chown --help' for more information.\n");
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage();
        return 0;
    }

    if (argc < 3) {
        write_str(STDERR_FILENO, "chown: missing operand after '");
        write_str(STDERR_FILENO, argv[1]);
        write_str(STDERR_FILENO, "'\n");
        write_str(STDERR_FILENO, "Try 'chown --help' for more information.\n");
        return 1;
    }

    char owner_buf[64];
    char group_buf[64];
    owner_buf[0] = '\0';
    group_buf[0] = '\0';

    const char *spec = argv[1];
    const char *colon = strchr(spec, ':');
    if (!colon) colon = strchr(spec, '.');

    if (colon) {
        size_t ulen = (size_t)(colon - spec);
        if (ulen >= sizeof(owner_buf)) ulen = sizeof(owner_buf) - 1;
        for (size_t i = 0; i < ulen; ++i) owner_buf[i] = spec[i];
        owner_buf[ulen] = '\0';

        const char *gstr = colon + 1;
        size_t glen = strlen(gstr);
        if (glen >= sizeof(group_buf)) glen = sizeof(group_buf) - 1;
        for (size_t i = 0; i < glen; ++i) group_buf[i] = gstr[i];
        group_buf[glen] = '\0';
    } else {
        size_t ulen = strlen(spec);
        if (ulen >= sizeof(owner_buf)) ulen = sizeof(owner_buf) - 1;
        for (size_t i = 0; i < ulen; ++i) owner_buf[i] = spec[i];
        owner_buf[ulen] = '\0';
    }

    uint32_t uid_val = 0;
    uint32_t gid_val = 0;
    int have_uid = 0;
    int have_gid = 0;

    if (owner_buf[0] != '\0') {
        if (!parse_id(owner_buf, &uid_val)) {
            write_str(STDERR_FILENO, "chown: invalid user: '");
            write_str(STDERR_FILENO, owner_buf);
            write_str(STDERR_FILENO, "'\n");
            return 1;
        }
        have_uid = 1;
    }

    if (group_buf[0] != '\0') {
        if (!parse_id(group_buf, &gid_val)) {
            write_str(STDERR_FILENO, "chown: invalid group: '");
            write_str(STDERR_FILENO, group_buf);
            write_str(STDERR_FILENO, "'\n");
            return 1;
        }
        have_gid = 1;
    }

    int exit_status = 0;
    for (int i = 2; i < argc; ++i) {
        const char *file_path = argv[i];
        struct akaos_stat st;
        if (stat(file_path, &st) < 0) {
            write_str(STDERR_FILENO, "chown: cannot access '");
            write_str(STDERR_FILENO, file_path);
            write_str(STDERR_FILENO, "': No such file or directory\n");
            exit_status = 1;
            continue;
        }

        uint32_t final_uid = have_uid ? uid_val : st.uid;
        uint32_t final_gid = have_gid ? gid_val : st.gid;

        if (chown(file_path, final_uid, final_gid) < 0) {
            write_str(STDERR_FILENO, "chown: changing ownership of '");
            write_str(STDERR_FILENO, file_path);
            if (errno == ENOENT) {
                write_str(STDERR_FILENO, "': No such file or directory\n");
            } else if (errno == EROFS) {
                write_str(STDERR_FILENO, "': Read-only file system\n");
            } else {
                write_str(STDERR_FILENO, "': Operation failed\n");
            }
            exit_status = 1;
        }
    }

    return exit_status;
}
