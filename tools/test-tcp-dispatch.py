#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise production TCP dispatch with synthetic hostile and valid segments."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'xscore/kernel/socket.c').read_text()

def function(name):
    start = source.index(name + '(')
    start = source.rfind('\n', 0, start) + 1
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define XSCORE_SMP_H
 typedef int xscore_spinlock_t;
static void xscore_spinlock_init(int *lock) { *lock = 0; }
static uintptr_t xscore_spin_lock_irqsave(int *lock) { assert(!*lock); *lock = 1; return 0; }
static void xscore_spin_unlock_irqrestore(int *lock, uintptr_t f) { (void)f; assert(*lock); *lock = 0; }
#include <xscore/socket.h>
static struct xscore_socket socket_pool[XSCORE_SOCKET_MAX];
static int socket_pool_lock, sent;
static void send_tcp_packet(struct xscore_socket *s, uint8_t f, const void *p, size_t n) {
    (void)s; (void)f; (void)p; (void)n; assert(socket_pool_lock); ++sent;
}
'''
for name in ['htons16', 'htonl32', 'socket_demux_tcp_locked', 'xscore_socket_demux_tcp']:
    harness += function(name)
harness += r'''
static void packet(uint32_t src, uint32_t dst, uint32_t seq, uint32_t ack,
                   uint8_t flags, int payload, int offset) {
    unsigned char buf[24] = {0};
    struct akaos_tcp_hdr *t = (void *)buf;
    t->src_port = htons16(80); t->dst_port = htons16(1234);
    t->seq = htonl32(seq); t->ack = htonl32(ack);
    t->flags = flags; t->data_offset_reserved = offset << 4;
    buf[20] = 'x';
    xscore_socket_demux_tcp(buf, 20 + payload, src, dst);
}
static struct xscore_socket *setup(int state) {
    memset(socket_pool, 0, sizeof(socket_pool)); sent = 0;
    struct xscore_socket *s = &socket_pool[0];
    s->in_use = 1; s->domain = AKAOS_AF_INET;
    s->type = AKAOS_SOCK_STREAM; s->protocol = AKAOS_IPPROTO_TCP;
    s->local_ip = 1; s->remote_ip = 2;
    s->local_port = 1234; s->remote_port = 80;
    s->seq = 1001; s->tcp_acked_seq = 1000; s->ack = 2001;
    s->state = state; s->pending_socket_id = -1;
    return s;
}
int main(void) {
    struct xscore_socket *s = setup(SOCKET_STATE_ESTABLISHED);
    struct xscore_socket original = *s;
    for (int f = 0; f < 64; ++f) {
        packet(3, 1, 2001, 1001, f, 1, 5);
        packet(2, 3, 2001, 1001, f, 1, 5);
        packet(2, 1, 2000, 1001, f, 1, 5);
        assert(memcmp(s, &original, sizeof(*s)) == 0);
    }
    packet(2, 1, 2001, 1002, AKAOS_TCP_FLAG_ACK, 1, 5);
    packet(2, 1, 2001, 999, AKAOS_TCP_FLAG_ACK, 1, 5);
    packet(2, 1, 2001, 1001, AKAOS_TCP_FLAG_ACK, 1, 0);
    packet(2, 1, 2001, 1001, AKAOS_TCP_FLAG_ACK, 1, 6);
    packet(2, 1, 2001, 1001, 0, 1, 5);
    assert(memcmp(s, &original, sizeof(*s)) == 0);
    s->domain = AKAOS_AF_UNIX;
    packet(2, 1, 2001, 1001, AKAOS_TCP_FLAG_ACK, 1, 5);
    assert(s->rx_count == 0); s->domain = AKAOS_AF_INET;
    packet(2, 1, 2001, 1001, AKAOS_TCP_FLAG_ACK, 1, 5);
    assert(s->rx_count == 1 && s->rx_buf[0] == 'x' && s->ack == 2002);
    packet(2, 1, 2001, 1001, AKAOS_TCP_FLAG_ACK, 1, 5);
    assert(s->rx_count == 1);
    packet(2, 1, 2002, 1001, AKAOS_TCP_FLAG_ACK | AKAOS_TCP_FLAG_FIN, 1, 5);
    assert(s->rx_count == 2 && s->ack == 2004 && s->state == SOCKET_STATE_CLOSE_WAIT);
    s = setup(SOCKET_STATE_SYN_SENT);
    packet(2, 1, 2000, 1001, AKAOS_TCP_FLAG_SYN, 0, 5);
    packet(2, 1, 2000, 1001, AKAOS_TCP_FLAG_ACK, 0, 5);
    packet(2, 1, 2000, 1002, AKAOS_TCP_FLAG_SYN | AKAOS_TCP_FLAG_ACK, 0, 5);
    assert(s->state == SOCKET_STATE_SYN_SENT);
    packet(2, 1, 2000, 1001, AKAOS_TCP_FLAG_SYN | AKAOS_TCP_FLAG_ACK, 0, 5);
    assert(s->state == SOCKET_STATE_ESTABLISHED && s->seq == 1001 && s->ack == 2001);
    s = setup(SOCKET_STATE_LISTEN);
    packet(2, 3, 2000, 0, AKAOS_TCP_FLAG_SYN, 0, 5);
    packet(2, 1, 2000, 0, AKAOS_TCP_FLAG_SYN | AKAOS_TCP_FLAG_FIN, 0, 5);
    assert(s->pending_socket_id == -1);
    packet(2, 1, 2000, 0, AKAOS_TCP_FLAG_SYN, 0, 5);
    assert(s->pending_socket_id == 1);
    struct xscore_socket *child = &socket_pool[1];
    assert(child->state == SOCKET_STATE_SYN_RECV && child->local_ip == 1);
    packet(2, 1, 2000, 0, AKAOS_TCP_FLAG_SYN, 0, 5);
    assert(!socket_pool[2].in_use && s->pending_socket_id == 1);
    packet(2, 1, 2001, 5000, AKAOS_TCP_FLAG_ACK, 0, 5);
    assert(child->state == SOCKET_STATE_SYN_RECV);
    packet(2, 1, 2001, 5001, AKAOS_TCP_FLAG_ACK, 1, 5);
    assert(child->state == SOCKET_STATE_ESTABLISHED && child->rx_count == 1);
    s = setup(SOCKET_STATE_ESTABLISHED);
    s->ack = UINT32_MAX; s->seq = 1; s->tcp_acked_seq = UINT32_MAX;
    packet(2, 1, UINT32_MAX, 0, AKAOS_TCP_FLAG_ACK, 1, 5);
    assert(s->ack == 0 && s->rx_count == 1);
    packet(2, 1, 0, 0, AKAOS_TCP_FLAG_RST, 0, 5);
    assert(s->state == SOCKET_STATE_CLOSED);
    s = setup(SOCKET_STATE_ESTABLISHED); s->rx_count = XSCORE_SOCKET_BUF_SIZE;
    packet(2, 1, 2001, 1001, AKAOS_TCP_FLAG_ACK | AKAOS_TCP_FLAG_FIN, 1, 5);
    assert(s->ack == 2001 && s->state == SOCKET_STATE_ESTABLISHED);
}
'''
with tempfile.TemporaryDirectory(prefix='akaos-tcp-') as temp:
    src = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    src.write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-g',
                    '-I' + str(ROOT / 'xscore/include'),
                    '-I' + str(ROOT / 'sdk/include'), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('TCP endpoint and state regression passed (ASan/UBSan)')
