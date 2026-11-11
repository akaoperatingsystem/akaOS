#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Host regression for cron's job identity and fail-closed execution boundary."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
harness = r'''
#include <assert.h>
#include <setjmp.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
static pid_t job_fork(void);
static struct passwd *job_account(const char *);
static int job_groups(size_t, const gid_t *);
static int job_gid(gid_t);
static int job_uid(uid_t);
static int job_exec(const char *, char *const []);
static void job_exit(int) __attribute__((noreturn));
#define fork job_fork
#define getpwnam job_account
#define setgroups job_groups
#define setgid job_gid
#define setuid job_uid
#define execv job_exec
#define _exit job_exit
#define main cron_main
#include "userspace/bin/cron/main.c"
#undef main
static jmp_buf done;
static int stage, failure, executed, status;
static pid_t fork_result;
static const char *expected_user;
static struct passwd account;
static pid_t job_fork(void) { return fork_result; }
static struct passwd *job_account(const char *name) {
    assert(stage++ == 0);
    assert(strcmp(name, expected_user) == 0);
    account.pw_uid = strcmp(name, "root") == 0 ? 0 : 1000;
    account.pw_gid = strcmp(name, "root") == 0 ? 0 : 100;
    return failure == 1 ? NULL : &account;
}
static int job_groups(size_t n, const gid_t *groups) {
    assert(stage++ == 1 && n == 0 && groups == NULL);
    return failure == 2 ? -1 : 0;
}
static int job_gid(gid_t gid) {
    assert(stage++ == 2 && gid == account.pw_gid);
    return failure == 3 ? -1 : 0;
}
static int job_uid(uid_t uid) {
    assert(stage++ == 3 && uid == account.pw_uid);
    return failure == 4 ? -1 : 0;
}
static int job_exec(const char *path, char *const args[]) {
    assert(stage++ == 4);
    assert(strcmp(path, "/bin/sh") == 0);
    assert(strcmp(args[0], path) == 0 && strcmp(args[1], "-c") == 0);
    assert(strcmp(args[2], "echo safe") == 0 && args[3] == NULL);
    executed++;
    return -1;
}
static void job_exit(int code) { status = code; longjmp(done, 1); }
static void reset(const char *user, int fail) {
    stage = executed = status = 0; failure = fail; expected_user = user;
    fork_result = 0;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    for (int fail = 0; fail <= 4; fail++) {
        reset("user", fail);
        if (!setjmp(done)) execute_job("user", "echo safe");
        assert(executed == (fail == 0));
        assert(status == (fail == 0 ? 127 : 126));
        assert(stage == (fail == 0 ? 5 : fail));
    }
    reset(NULL, 0);
    if (!setjmp(done)) execute_job(NULL, "echo safe");
    assert(stage == 0 && executed == 0 && status == 126);
    for (int i = 0; i < 2; i++) {
        reset("user", 0); fork_result = i == 0 ? 123 : -1;
        execute_job("user", "echo safe");
        assert(stage == 0 && executed == 0);
    }
    /* Exercise both system user fields and per-user spool identities. */
    for (int i = 0; i < 3; i++) {
        FILE *fp = fopen(argv[1], "w"); assert(fp);
        fputs(i == 0 ? "* * * * * user echo safe\n" :
              i == 1 ? "* * * * * echo safe\n" :
                       "* * * * * root echo safe\n", fp);
        fclose(fp);
        reset(i == 2 ? "root" : "user", 0);
        struct tm now = {.tm_mday = 1};
        if (!setjmp(done)) process_crontab_file(argv[1], i == 1 ? "user" : NULL, &now);
        assert(executed == 1 && status == 127);
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-cron-') as directory:
    tmp = Path(directory)
    source = tmp / 'test.c'
    binary = tmp / 'test'
    source.write_text(harness)
    subprocess.run(['cc', '-std=c11', '-D_DEFAULT_SOURCE', '-Wall', '-Wextra',
                    '-Werror', '-I', str(ROOT), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary), str(tmp / 'crontab')], check=True)
print('cron credential regressions passed')
