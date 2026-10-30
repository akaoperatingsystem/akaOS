#include <akaos/account.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc > 1) {
        if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
            printf("Usage: whoami\nPrint the current user name.\n");
            return 0;
        }
        fprintf(stderr, "whoami: extra operand '%s'\n", argv[1]);
        return 1;
    }
    struct akaos_account account;
    if (akaos_account_by_uid(geteuid(), &account)) {
        puts(account.name);
    } else {
        printf("%u\n", (unsigned)geteuid());
    }
    return 0;
}
