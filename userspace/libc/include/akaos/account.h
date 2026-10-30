#ifndef AKAOS_ACCOUNT_H
#define AKAOS_ACCOUNT_H

#include <stddef.h>
#include <sys/types.h>

#define AKAOS_ACCOUNT_NAME_MAX 31
#define AKAOS_ACCOUNT_PATH_MAX 127
#define AKAOS_PASSWORD_MAX 63

struct akaos_account {
    char name[AKAOS_ACCOUNT_NAME_MAX + 1];
    uid_t uid;
    gid_t gid;
    char home[AKAOS_ACCOUNT_PATH_MAX + 1];
    char shell[AKAOS_ACCOUNT_PATH_MAX + 1];
};

int akaos_account_by_name(const char *name, struct akaos_account *account);
int akaos_account_by_uid(uid_t uid, struct akaos_account *account);
int akaos_account_verify(const struct akaos_account *account,
                         const char *password);
int akaos_account_set_password(const struct akaos_account *account,
                               const char *password);
int akaos_read_secret(const char *prompt, char *buffer, size_t capacity);

#endif
