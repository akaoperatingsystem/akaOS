#include <akaos/account.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        puts("Usage: su [USER]\nAuthenticate and run a shell as USER (default: root).");
        return 0;
    }
    if (argc > 2 || (argc == 2 && argv[1][0] == '-')) {
        puts("su: invalid option or extra operand");
        return 1;
    }
    const char *name = argc > 1 ? argv[1] : "root";
    struct akaos_account account;
    if (!akaos_account_by_name(name, &account)) {
        fprintf(stderr, "su: unknown user '%s'\n", name);
        return 1;
    }
    if (getuid() != 0) {
        char password[AKAOS_PASSWORD_MAX + 1];
        if (!akaos_read_secret("Password: ", password, sizeof(password)) ||
            !akaos_account_verify(&account, password)) {
            memset(password, 0, sizeof(password));
            puts("su: Authentication failure");
            return 1;
        }
        memset(password, 0, sizeof(password));
    }
    if (setgid(account.gid) != 0 || setuid(account.uid) != 0) {
        puts("su: unable to establish credentials");
        return 1;
    }
    if (chdir(account.home) != 0) chdir("/");
    char *shell_argv[] = {account.shell, NULL};
    int pid = spawn(account.shell, shell_argv);
    if (pid < 0) return 1;
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return 1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
