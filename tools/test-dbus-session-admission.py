#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise the broker's admission boundary with kernel credential fixtures."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'userspace/bin/dbus-daemon/main.c').read_text()
handler = source[source.index('static void handle_client('):
                 source.index('static void *unix_accept_worker(')]
# Stub protocol processing: admission must happen before any client input is read.
harness = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define AKAOS_DBUS_MAX_MESSAGE 512U
static int dbus_session_bus, reads, closes, credential_error, short_credentials;
static uid_t owner_uid, peer_uid;
struct dbus_client_context {
    int used, fd;
    pthread_t thread;
    struct ucred peer_credentials;
};
static pthread_mutex_t dbus_state_lock = PTHREAD_MUTEX_INITIALIZER;
static int dbus_trace_enabled(void) { return 0; }
static uid_t mock_geteuid(void) { return owner_uid; }
static int mock_close(int fd) { assert(fd == 42); ++closes; return 0; }
static int mock_getsockopt(int fd, int level, int option, void *value,
                           socklen_t *length) {
    assert(fd == 42 && level == SOL_SOCKET && option == SO_PEERCRED);
    struct ucred credentials = {.pid = 123, .uid = peer_uid, .gid = 1000};
    memcpy(value, &credentials, sizeof(credentials));
    *length = sizeof(credentials) - short_credentials;
    return credential_error ? -1 : 0;
}
static int recv_bootstrap_line(int fd, unsigned char *buffer, size_t size,
                               ssize_t *count) {
    (void)fd; (void)buffer; (void)size; (void)count;
    ++reads;
    return 0;
}
static int authenticate_standard_client(int fd, unsigned char *b, ssize_t n,
                                        uint32_t uid) {
    (void)fd; (void)b; (void)n; (void)uid; assert(0); return 0;
}
static struct dbus_client_context *register_binary_client(
    int fd, const struct ucred *credentials) {
    (void)fd; (void)credentials; assert(0); return NULL;
}
static void *binary_client_worker(void *context) { return context; }
static int send_all(int fd, const char *s) {
    (void)fd; (void)s; assert(0); return 0;
}
static int recv_line(int fd, char *b, size_t n) {
    (void)fd; (void)b; (void)n; assert(0); return 0;
}
#define geteuid mock_geteuid
#define getsockopt mock_getsockopt
#define close mock_close
'''
cases = r'''
int main(void) {
    for (unsigned owner = 0; owner < 2; ++owner) {
        owner_uid = owner ? 1000 : 0;
        for (unsigned peer = 0; peer < 3; ++peer) {
            peer_uid = peer == 0 ? 0 : peer == 1 ? 1000 : 1001;
            for (int session = 0; session <= 1; ++session) {
                dbus_session_bus = session;
                for (int transport = 0; transport <= 1; ++transport) {
                    for (int error = 0; error < 3; ++error) {
                        credential_error = error == 1;
                        short_credentials = error == 2;
                        reads = closes = 0;
                        handle_client(42, transport);
                        assert(closes == 1);
                        assert(reads == (transport && error == 0 &&
                                        (!session || peer_uid == owner_uid)));
                    }
                }
            }
        }
    }
    puts("D-Bus session admission: 72 cases passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'test.c').write_text(harness + handler + cases)
    subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-pthread',
                    str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
