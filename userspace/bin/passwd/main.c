#include <akaos/account.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: passwd [USER]\nChange an akaOS account password.");
        return 0;
    }
    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        puts("passwd: invalid option or extra operand");
        return 1;
    }
    struct akaos_account caller, target;
    if (!akaos_account_by_uid(getuid(), &caller)) {
        puts("passwd: current account is unknown");
        return 1;
    }
    const char *target_name = argc > 1 ? argv[1] : caller.name;
    if (argc > 2 || (getuid() != 0 && strcmp(target_name, caller.name) != 0)) {
        puts("passwd: permission denied");
        return 1;
    }
    if (!akaos_account_by_name(target_name, &target)) {
        puts("passwd: unknown account");
        return 1;
    }
    char old_password[AKAOS_PASSWORD_MAX + 1];
    if (getuid() != 0) {
        if (!akaos_read_secret("Current password: ", old_password, sizeof(old_password)) ||
            !akaos_account_verify(&caller, old_password)) {
            memset(old_password, 0, sizeof(old_password));
            puts("passwd: Authentication failure");
            return 1;
        }
        memset(old_password, 0, sizeof(old_password));
    }
    char first[AKAOS_PASSWORD_MAX + 1], second[AKAOS_PASSWORD_MAX + 1];
    if (!akaos_read_secret("New password: ", first, sizeof(first)) ||
        !akaos_read_secret("Retype new password: ", second, sizeof(second))) return 1;
    if (strlen(first) < 6) {
        puts("passwd: password must contain at least 6 characters");
        return 1;
    }
    if (strcmp(first, second) != 0) {
        puts("passwd: passwords do not match");
        return 1;
    }
    int changed = akaos_account_set_password(&target, first);
    memset(first, 0, sizeof(first)); memset(second, 0, sizeof(second));
    if (!changed) {
        puts("passwd: password update failed");
        return 1;
    }
    puts("passwd: password updated successfully");
    return 0;
}
