#include <akaos/account.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ACCOUNT_FILE_MAX 4096
#define PASSWORD_ROUNDS 4096

/* User stacks are intentionally small. Keep file-sized account workspaces in
 * process-local BSS instead of consuming multiple stack pages. */
static char account_buffer[ACCOUNT_FILE_MAX];
static char account_output_buffer[ACCOUNT_FILE_MAX];

struct sha256_context {
    uint32_t state[8];
    uint64_t bits;
    uint8_t block[64];
    size_t used;
};

static uint32_t rotate_right(uint32_t value, uint32_t shift) {
    return (value >> shift) | (value << (32 - shift));
}

static void sha256_transform(struct sha256_context *context,
                             const uint8_t block[64]) {
    static const uint32_t constants[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t words[64];
    for (size_t i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)block[i * 4] << 24) |
                   ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) | block[i * 4 + 3];
    }
    for (size_t i = 16; i < 64; ++i) {
        uint32_t s0 = rotate_right(words[i - 15], 7) ^
                      rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
        uint32_t s1 = rotate_right(words[i - 2], 17) ^
                      rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    uint32_t a=context->state[0], b=context->state[1], c=context->state[2], d=context->state[3];
    uint32_t e=context->state[4], f=context->state[5], g=context->state[6], h=context->state[7];
    for (size_t i = 0; i < 64; ++i) {
        uint32_t s1=rotate_right(e,6)^rotate_right(e,11)^rotate_right(e,25);
        uint32_t choice=(e&f)^((~e)&g);
        uint32_t t1=h+s1+choice+constants[i]+words[i];
        uint32_t s0=rotate_right(a,2)^rotate_right(a,13)^rotate_right(a,22);
        uint32_t majority=(a&b)^(a&c)^(b&c);
        uint32_t t2=s0+majority;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    context->state[0]+=a; context->state[1]+=b; context->state[2]+=c; context->state[3]+=d;
    context->state[4]+=e; context->state[5]+=f; context->state[6]+=g; context->state[7]+=h;
}

static void sha256_init(struct sha256_context *context) {
    static const uint32_t initial[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(context->state, initial, sizeof(initial));
    context->bits = 0; context->used = 0;
}

static void sha256_update(struct sha256_context *context, const void *input,
                          size_t length) {
    const uint8_t *bytes = input;
    context->bits += (uint64_t)length * 8;
    while (length) {
        size_t count = 64 - context->used;
        if (count > length) count = length;
        memcpy(context->block + context->used, bytes, count);
        context->used += count; bytes += count; length -= count;
        if (context->used == 64) {
            sha256_transform(context, context->block);
            context->used = 0;
        }
    }
}

static void sha256_final(struct sha256_context *context, uint8_t digest[32]) {
    context->block[context->used++] = 0x80;
    if (context->used > 56) {
        while (context->used < 64) context->block[context->used++] = 0;
        sha256_transform(context, context->block); context->used = 0;
    }
    while (context->used < 56) context->block[context->used++] = 0;
    for (size_t i = 0; i < 8; ++i) {
        context->block[63 - i] = (uint8_t)(context->bits >> (i * 8));
    }
    sha256_transform(context, context->block);
    for (size_t i = 0; i < 8; ++i) {
        digest[i*4]=(uint8_t)(context->state[i]>>24);
        digest[i*4+1]=(uint8_t)(context->state[i]>>16);
        digest[i*4+2]=(uint8_t)(context->state[i]>>8);
        digest[i*4+3]=(uint8_t)context->state[i];
    }
}

static void password_hash(const char *name, const char *salt,
                          const char *password, char output[65]) {
    uint8_t digest[32];
    struct sha256_context context;
    sha256_init(&context);
    sha256_update(&context, "akaOS:", 6);
    if (salt && salt[0]) {
        sha256_update(&context, salt, strlen(salt));
    } else {
        sha256_update(&context, name, strlen(name));
    }
    sha256_update(&context, ":", 1);
    sha256_update(&context, password, strlen(password));
    sha256_final(&context, digest);
    for (unsigned round = 1; round < PASSWORD_ROUNDS; ++round) {
        sha256_init(&context);
        sha256_update(&context, digest, sizeof(digest));
        if (salt && salt[0]) {
            sha256_update(&context, salt, strlen(salt));
        } else {
            sha256_update(&context, name, strlen(name));
        }
        sha256_update(&context, ":", 1);
        sha256_update(&context, password, strlen(password));
        sha256_final(&context, digest);
    }
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        output[i*2] = hex[digest[i] >> 4];
        output[i*2+1] = hex[digest[i] & 15];
    }
    output[64] = 0;
    memset(digest, 0, sizeof(digest));
}

static void generate_salt(char output[17]) {
    static uint64_t seed = 0x5a5aa5a512345678ULL;
    uint64_t v = seed ^ ((uint64_t)getpid() << 32) ^ (uint64_t)(uintptr_t)&seed;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        v ^= v << 13;
        v ^= v >> 7;
        v ^= v << 17;
        output[i] = hex[v & 0xf];
    }
    output[16] = 0;
    seed = v;
}

static int read_file(const char *path, char *buffer, size_t capacity) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    size_t used = 0;
    while (used + 1 < capacity) {
        ssize_t count = read(fd, buffer + used, capacity - used - 1);
        if (count < 0) { close(fd); return -1; }
        if (count == 0) break;
        used += (size_t)count;
    }
    buffer[used] = 0;
    close(fd);
    return (int)used;
}

static unsigned parse_number(const char *text) {
    unsigned value = 0;
    while (*text >= '0' && *text <= '9') value = value * 10 + (unsigned)(*text++ - '0');
    return value;
}

static int copy_field(char *destination, size_t capacity, const char *start,
                      const char *end) {
    size_t length = (size_t)(end - start);
    if (length >= capacity) return 0;
    memcpy(destination, start, length); destination[length] = 0;
    return 1;
}

static int parse_account_line(const char *line, const char *end,
                              struct akaos_account *account) {
    const char *fields[7]; fields[0] = line;
    unsigned found = 1;
    for (const char *p = line; p < end && found < 7; ++p) {
        if (*p == ':') fields[found++] = p + 1;
    }
    if (found != 7) return 0;
    const char *ends[7];
    for (unsigned i = 0; i < 6; ++i) ends[i] = fields[i + 1] - 1;
    ends[6] = end;
    if (!copy_field(account->name, sizeof(account->name), fields[0], ends[0]) ||
        !copy_field(account->home, sizeof(account->home), fields[5], ends[5]) ||
        !copy_field(account->shell, sizeof(account->shell), fields[6], ends[6])) return 0;
    account->uid = parse_number(fields[2]); account->gid = parse_number(fields[3]);
    return 1;
}

static int find_account(const char *name, uid_t uid, int by_uid,
                        struct akaos_account *account) {
    char *file = account_buffer;
    int length = read_file("/etc/passwd", file, ACCOUNT_FILE_MAX);
    if (length < 0) return 0;
    const char *cursor = file, *limit = file + length;
    while (cursor < limit) {
        const char *end = cursor;
        while (end < limit && *end != '\n') ++end;
        struct akaos_account candidate;
        if (parse_account_line(cursor, end, &candidate) &&
            ((by_uid && candidate.uid == uid) || (!by_uid && strcmp(candidate.name, name) == 0))) {
            *account = candidate; return 1;
        }
        cursor = end < limit ? end + 1 : limit;
    }
    return 0;
}

int akaos_account_by_name(const char *name, struct akaos_account *account) {
    return name && account && find_account(name, 0, 0, account);
}

int akaos_account_by_uid(uid_t uid, struct akaos_account *account) {
    return account && find_account(0, uid, 1, account);
}

static int passwd_copy_string(char **destination, char **cursor,
                              size_t *remaining, const char *source) {
    size_t length = strlen(source) + 1U;
    if (length > *remaining) return ERANGE;
    memcpy(*cursor, source, length);
    *destination = *cursor;
    *cursor += length;
    *remaining -= length;
    return 0;
}

static int passwd_reentrant(const char *name, uid_t uid, int by_uid,
                            struct passwd *entry, char *buffer,
                            size_t buffer_size, struct passwd **result) {
    if (!entry || !buffer || !result || (!by_uid && !name)) return EINVAL;
    *result = NULL;
    struct akaos_account account;
    int found = by_uid ? akaos_account_by_uid(uid, &account)
                       : akaos_account_by_name(name, &account);
    if (!found) return 0;

    char *cursor = buffer;
    size_t remaining = buffer_size;
    int error = passwd_copy_string(&entry->pw_name, &cursor, &remaining,
                                   account.name);
    if (error != 0) return error;
    error = passwd_copy_string(&entry->pw_passwd, &cursor, &remaining, "x");
    if (error != 0) return error;
    error = passwd_copy_string(&entry->pw_gecos, &cursor, &remaining,
                               account.name);
    if (error != 0) return error;
    error = passwd_copy_string(&entry->pw_dir, &cursor, &remaining,
                               account.home);
    if (error != 0) return error;
    error = passwd_copy_string(&entry->pw_shell, &cursor, &remaining,
                               account.shell);
    if (error != 0) return error;
    entry->pw_uid = account.uid;
    entry->pw_gid = account.gid;
    *result = entry;
    return 0;
}

int getpwnam_r(const char *name, struct passwd *entry, char *buffer,
               size_t buffer_size, struct passwd **result) {
    return passwd_reentrant(name, 0U, 0, entry, buffer, buffer_size, result);
}

int getpwuid_r(uid_t uid, struct passwd *entry, char *buffer,
               size_t buffer_size, struct passwd **result) {
    return passwd_reentrant(NULL, uid, 1, entry, buffer, buffer_size, result);
}

static struct passwd passwd_static_entry;
static char passwd_static_buffer[AKAOS_ACCOUNT_PATH_MAX * 2U +
                                 AKAOS_ACCOUNT_NAME_MAX * 3U + 16U];

struct passwd *getpwnam(const char *name) {
    struct passwd *result = NULL;
    int error = getpwnam_r(name, &passwd_static_entry, passwd_static_buffer,
                           sizeof(passwd_static_buffer), &result);
    if (error != 0) { errno = error; return NULL; }
    if (!result) errno = ENOENT;
    return result;
}

struct passwd *getpwuid(uid_t uid) {
    struct passwd *result = NULL;
    int error = getpwuid_r(uid, &passwd_static_entry, passwd_static_buffer,
                           sizeof(passwd_static_buffer), &result);
    if (error != 0) { errno = error; return NULL; }
    if (!result) errno = ENOENT;
    return result;
}

static int akaos_pwent_file_len = -1;
static size_t akaos_pwent_offset = 0U;

void setpwent(void) {
    akaos_pwent_offset = 0U;
    akaos_pwent_file_len = read_file("/etc/passwd", account_buffer, ACCOUNT_FILE_MAX);
}

void endpwent(void) {
    akaos_pwent_offset = 0U;
    akaos_pwent_file_len = -1;
}

struct passwd *getpwent(void) {
    if (akaos_pwent_file_len < 0) {
        akaos_pwent_file_len = read_file("/etc/passwd", account_buffer, ACCOUNT_FILE_MAX);
        akaos_pwent_offset = 0U;
    }
    if (akaos_pwent_file_len <= 0) return NULL;

    const char *file = account_buffer;
    const char *limit = file + akaos_pwent_file_len;
    const char *cursor = file + akaos_pwent_offset;

    while (cursor < limit) {
        const char *end = cursor;
        while (end < limit && *end != '\n') ++end;
        struct akaos_account account;
        if (parse_account_line(cursor, end, &account)) {
            akaos_pwent_offset = (size_t)((end < limit ? end + 1 : limit) - file);
            char *buf_cursor = passwd_static_buffer;
            size_t remaining = sizeof(passwd_static_buffer);
            passwd_copy_string(&passwd_static_entry.pw_name, &buf_cursor, &remaining, account.name);
            passwd_copy_string(&passwd_static_entry.pw_passwd, &buf_cursor, &remaining, "x");
            passwd_static_entry.pw_uid = account.uid;
            passwd_static_entry.pw_gid = account.gid;
            passwd_copy_string(&passwd_static_entry.pw_gecos, &buf_cursor, &remaining, account.name);
            passwd_copy_string(&passwd_static_entry.pw_dir, &buf_cursor, &remaining, account.home);
            passwd_copy_string(&passwd_static_entry.pw_shell, &buf_cursor, &remaining, account.shell);
            return &passwd_static_entry;
        }
        cursor = end < limit ? end + 1 : limit;
    }
    return NULL;
}

struct akaos_group_record {
    const char *name;
    gid_t gid;
};

static const struct akaos_group_record akaos_groups[] = {
    {"root", 0U},
    {"wheel", 0U},
    {"users", 100U},
    {"nogroup", 65534U},
};
static char *akaos_empty_group_members[] = {NULL};

static const struct akaos_group_record *group_by_name(const char *name) {
    if (!name) return NULL;
    for (size_t index = 0U;
         index < sizeof(akaos_groups) / sizeof(akaos_groups[0]); ++index)
        if (strcmp(name, akaos_groups[index].name) == 0)
            return &akaos_groups[index];
    return NULL;
}

static const struct akaos_group_record *group_by_gid(gid_t gid) {
    for (size_t index = 0U;
         index < sizeof(akaos_groups) / sizeof(akaos_groups[0]); ++index)
        if (gid == akaos_groups[index].gid)
            return &akaos_groups[index];
    return NULL;
}

static int group_record_fill(const struct akaos_group_record *record,
                             struct group *entry, char *buffer,
                             size_t buffer_size, struct group **result) {
    if (!entry || !buffer || !result) return EINVAL;
    *result = NULL;
    if (!record) return 0;
    size_t name_length = strlen(record->name) + 1U;
    if (buffer_size < name_length + 2U) return ERANGE;
    memcpy(buffer, record->name, name_length);
    entry->gr_name = buffer;
    entry->gr_passwd = buffer + name_length;
    entry->gr_passwd[0] = 'x';
    entry->gr_passwd[1] = '\0';
    entry->gr_gid = record->gid;
    entry->gr_mem = akaos_empty_group_members;
    *result = entry;
    return 0;
}

int getgrnam_r(const char *name, struct group *entry, char *buffer,
               size_t buffer_size, struct group **result) {
    if (!name) return EINVAL;
    return group_record_fill(group_by_name(name), entry, buffer,
                             buffer_size, result);
}

int getgrgid_r(gid_t gid, struct group *entry, char *buffer,
               size_t buffer_size, struct group **result) {
    return group_record_fill(group_by_gid(gid), entry, buffer,
                             buffer_size, result);
}

static struct group akaos_static_group;
static char akaos_static_group_buffer[AKAOS_ACCOUNT_NAME_MAX + 8U];
static size_t akaos_group_iteration;

struct group *getgrnam(const char *name) {
    struct group *result = NULL;
    int error = getgrnam_r(name, &akaos_static_group,
                           akaos_static_group_buffer,
                           sizeof(akaos_static_group_buffer), &result);
    if (error != 0) { errno = error; return NULL; }
    if (!result) errno = ENOENT;
    return result;
}

struct group *getgrgid(gid_t gid) {
    struct group *result = NULL;
    int error = getgrgid_r(gid, &akaos_static_group,
                           akaos_static_group_buffer,
                           sizeof(akaos_static_group_buffer), &result);
    if (error != 0) { errno = error; return NULL; }
    if (!result) errno = ENOENT;
    return result;
}

void setgrent(void) { akaos_group_iteration = 0U; }
void endgrent(void) { akaos_group_iteration = 0U; }

struct group *getgrent(void) {
    if (akaos_group_iteration >=
        sizeof(akaos_groups) / sizeof(akaos_groups[0]))
        return NULL;
    const struct akaos_group_record *record =
        &akaos_groups[akaos_group_iteration++];
    struct group *result = NULL;
    int error = group_record_fill(record, &akaos_static_group,
                                  akaos_static_group_buffer,
                                  sizeof(akaos_static_group_buffer), &result);
    if (error != 0) { errno = error; return NULL; }
    return result;
}

int setgroups(size_t size, const gid_t *list) {
    (void)size;
    (void)list;
    return 0;
}

static int shadow_hash(const char *name, char salt[33], char hash[65]) {
    char *file = account_buffer;
    int length = read_file("/etc/shadow", file, ACCOUNT_FILE_MAX);
    if (length < 0) return 0;
    const char *cursor=file, *limit=file+length;
    while (cursor < limit) {
        const char *colon=cursor, *end=cursor;
        while (colon < limit && *colon != ':' && *colon != '\n') ++colon;
        while (end < limit && *end != '\n') ++end;
        if (colon < end && (size_t)(colon-cursor)==strlen(name) &&
            strncmp(cursor,name,(size_t)(colon-cursor))==0) {
            const char *val = colon + 1;
            size_t val_len = (size_t)(end - val);
            if (val_len == 0 || val[0] == '!' || val[0] == '*') {
                /* Account is locked */
                return 0;
            }
            /* Check if formatted as salt$hash */
            const char *dollar = NULL;
            for (const char *p = val; p < end; ++p) {
                if (*p == '$') { dollar = p; break; }
            }
            if (dollar != NULL) {
                size_t salt_len = (size_t)(dollar - val);
                const char *hash_val = dollar + 1;
                size_t hash_len = (size_t)(end - hash_val);
                if (salt_len > 0 && salt_len <= 32 && hash_len == 64) {
                    memcpy(salt, val, salt_len);
                    salt[salt_len] = 0;
                    memcpy(hash, hash_val, 64);
                    hash[64] = 0;
                    return 1;
                }
            } else if (val_len == 64) {
                /* Legacy unsalted format */
                salt[0] = 0;
                memcpy(hash, val, 64);
                hash[64] = 0;
                return 1;
            }
        }
        cursor=end<limit?end+1:limit;
    }
    return 0;
}

int akaos_account_verify(const struct akaos_account *account,
                         const char *password) {
    char salt[33], expected[65], actual[65];
    if (!account || !password || !shadow_hash(account->name, salt, expected)) return 0;
    password_hash(account->name, salt, password, actual);
    unsigned difference = 0;
    for (size_t i=0;i<64;++i) difference |= (unsigned)(expected[i]^actual[i]);
    memset(actual,0,sizeof(actual)); return difference == 0;
}

int akaos_account_set_password(const struct akaos_account *account,
                               const char *password) {
    if (!account || !password || strlen(password) > AKAOS_PASSWORD_MAX) return 0;
    char *source=account_buffer, *output=account_output_buffer;
    char salt[17], hash[65];
    int length=read_file("/etc/shadow",source,ACCOUNT_FILE_MAX);
    if (length < 0) return 0;
    generate_salt(salt);
    password_hash(account->name,salt,password,hash);
    size_t used=0; const char *cursor=source,*limit=source+length; int replaced=0;
    while(cursor<limit) {
        const char *colon=cursor,*end=cursor;
        while(colon<limit&&*colon!=':'&&*colon!='\n')++colon;
        while(end<limit&&*end!='\n')++end;
        int match=colon<end&&(size_t)(colon-cursor)==strlen(account->name)&&
                  strncmp(cursor,account->name,(size_t)(colon-cursor))==0;
        size_t needed=match?strlen(account->name)+1+16+1+64:(size_t)(end-cursor);
        if(used+needed+1>=ACCOUNT_FILE_MAX) return 0;
        if(match){
            size_t nlen = strlen(account->name);
            memcpy(output+used,account->name,nlen); used+=nlen;
            output[used++]=':';
            memcpy(output+used,salt,16); used+=16;
            output[used++]='$';
            memcpy(output+used,hash,64); used+=64;
            replaced=1;
        }
        else{memcpy(output+used,cursor,(size_t)(end-cursor));used+=(size_t)(end-cursor);}
        output[used++]='\n'; cursor=end<limit?end+1:limit;
    }
    if(!replaced)return 0;
    int fd=open("/etc/shadow",O_WRONLY|O_TRUNC); if(fd<0)return 0;
    size_t written=0; while(written<used){ssize_t count=write(fd,output+written,used-written);if(count<=0){close(fd);return 0;}written+=(size_t)count;}
    close(fd); sync(); memset(hash,0,sizeof(hash)); return 1;
}

int akaos_read_secret(const char *prompt, char *buffer, size_t capacity) {
    if (!buffer || capacity < 2) return 0;
    if (prompt) write(STDOUT_FILENO,prompt,strlen(prompt));
    tty_set_echo(0); size_t used=0;
    for (;;) {
        char c; ssize_t count=read(STDIN_FILENO,&c,1);
        if(count<=0){tty_set_echo(1);return 0;}
        if(c=='\n'||c=='\r')break;
        if(used+1<capacity)buffer[used++]=c;
    }
    tty_set_echo(1); buffer[used]=0; return 1;
}
