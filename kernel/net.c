/* ============================================================
 * akaOS — Network Stack (PCI + e1000 + ARP/IP/ICMP)
 * ============================================================ */
#include "net.h"
#include "arch.h"
#include "io.h"
#include "string.h"
#include "time.h"
#include "limine.h"

extern void gui_pump(void);
extern volatile int gui_mode_active;
extern uint64_t hhdm_offset;
extern volatile struct limine_memmap_request memmap_request;
extern uint64_t kernel_phys_base;
extern uint64_t kernel_virt_base;

static uint64_t dma_phys(const void *vptr) {
    uint64_t v = (uint64_t)(uintptr_t)vptr;
    /* If this is an HHDM-mapped pointer, phys = virt - hhdm_offset. */
    if (hhdm_offset && v >= hhdm_offset)
        return v - hhdm_offset;
    /* Otherwise, if it lives in the kernel image mapping, translate using Limine bases. */
    if (kernel_virt_base && kernel_phys_base && v >= kernel_virt_base)
        return v - kernel_virt_base + kernel_phys_base;
    /* Best-effort fallback (may be wrong). */
    return v;
}

/* ============================================================
 * Very small physical allocator (top-down) for NIC DMA buffers.
 * We take memory from the highest usable Limine memmap entry so
 * we don't collide with the kernel image loaded low.
 * ============================================================ */
static uint64_t net_pmm_base = 0;
static uint64_t net_pmm_top  = 0;
static int net_pmm_ready = 0;

static uint64_t align_down_u64(uint64_t v, uint64_t a) {
    if (a == 0) return v;
    return v & ~(a - 1);
}

static void net_pmm_init(void) {
    if (net_pmm_ready) return;
    net_pmm_ready = 1;
    if (!memmap_request.response) return;

    uint64_t best_base = 0, best_end = 0;
    for (uint64_t i = 0; i < memmap_request.response->entry_count; i++) {
        struct limine_memmap_entry *e = memmap_request.response->entries[i];
        if (!e) continue;
        if (e->type != LIMINE_MEMMAP_USABLE) continue;
        if (e->length < 0x20000) continue;
        uint64_t end = e->base + e->length;
        if (end > best_end) {
            best_end = end;
            best_base = e->base;
        }
    }
    if (best_end > best_base) {
        net_pmm_base = best_base;
        net_pmm_top  = best_end;
    }
}

static void *net_pmm_alloc(size_t size, size_t align, uint64_t *out_phys) {
    net_pmm_init();
    if (!net_pmm_top || !hhdm_offset) return 0;
    if (align == 0) align = 16;
    if (align & (align - 1)) align = 16;

    uint64_t top = net_pmm_top;
    uint64_t p = align_down_u64(top - (uint64_t)size, (uint64_t)align);
    if (p < net_pmm_base || p >= top) return 0;
    net_pmm_top = p;
    if (out_phys) *out_phys = p;
    void *v = (void *)(uintptr_t)(hhdm_offset + p);
    memset(v, 0, size);
    return v;
}

/* ============================================================
 * PCI — proper 32-bit I/O
 * ============================================================ */
static void pci_write32(uint16_t port, uint32_t val) {
    outl(port, val);
}
static uint32_t pci_read32(uint16_t port) {
    return inl(port);
}
static uint32_t pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t addr = (1u<<31)|((uint32_t)bus<<16)|((uint32_t)dev<<11)
                   |((uint32_t)func<<8)|(off & 0xFC);
    pci_write32(0xCF8, addr);
    return pci_read32(0xCFC);
}
static void pci_config_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val) {
    uint32_t addr = (1u<<31)|((uint32_t)bus<<16)|((uint32_t)dev<<11)
                   |((uint32_t)func<<8)|(off & 0xFC);
    pci_write32(0xCF8, addr);
    pci_write32(0xCFC, val);
}

/* ============================================================
 * e1000 NIC
 * ============================================================ */
#define E1000_VID 0x8086
#define E1000_DID 0x100E
#define REG_CTRL  0x0000
#define REG_STATUS 0x0008
#define REG_RCTL  0x0100
#define REG_TCTL  0x0400
#define REG_RDBAL 0x2800
#define REG_RDLEN 0x2808
#define REG_RDH   0x2810
#define REG_RDT   0x2818
#define REG_TDBAL 0x3800
#define REG_TDLEN 0x3808
#define REG_TDH   0x3810
#define REG_TDT   0x3818
#define REG_RAL   0x5400
#define REG_RAH   0x5404
#define REG_MTA   0x5200
#define REG_IMS   0x00D0
#define REG_IMC   0x00D8
#define REG_ICR   0x00C0

#define CTRL_RST   (1u << 26)
#define CTRL_SLU   (1u << 6)    /* Set Link Up */
#define CTRL_ASDE  (1u << 5)    /* Auto-Speed Detection Enable */
#define STATUS_LU  (1u << 1)    /* Link Up */

#define NUM_DESC 8
#define BUF_SZ   2048

struct tx_desc { uint64_t addr; uint16_t len; uint8_t cso,cmd,sta,css; uint16_t spc; } __attribute__((packed));
struct rx_desc { uint64_t addr; uint16_t len; uint16_t csum; uint8_t sta,err; uint16_t spc; } __attribute__((packed));

static struct tx_desc *txd = 0;
static struct rx_desc *rxd = 0;
static uint8_t *tx_buf = 0; /* NUM_DESC * BUF_SZ */
static uint8_t *rx_buf = 0; /* NUM_DESC * BUF_SZ */

static volatile uint32_t *mmio = 0;
static uint8_t mac[6];
static int nic_ok = 0;
static int tx_i = 0, rx_i = 0;
static uint32_t our_ip, gw_ip;
static uint32_t tx_packets = 0, rx_packets = 0;

static void ew(uint32_t r, uint32_t v) { mmio[r/4]=v; }
static uint32_t er(uint32_t r) { return mmio[r/4]; }

static void e1000_send(uint8_t *data, uint16_t len) {
    if (!nic_ok) return;
    if (!txd || !tx_buf) return;
    uint8_t *buf = tx_buf + (size_t)tx_i * BUF_SZ;
    memcpy(buf, data, len);
    /* e1000 DMA addresses must be physical. */
    txd[tx_i].addr = dma_phys(buf);
    txd[tx_i].len = len;
    txd[tx_i].cmd = 0x0B; /* EOP + IFCS + RS */
    txd[tx_i].sta = 0;
    int o = tx_i;
    tx_i = (tx_i+1) % NUM_DESC;
    ew(REG_TDT, tx_i);
    /* Wait for TX complete (with timeout) */
    for (int t=0; t<500000 && !(txd[o].sta&1); t++) {
        arch_halt();
    }
    tx_packets++;
}

static int e1000_recv(uint8_t *buf, uint16_t *len) {
    if (!nic_ok) return 0;
    if (!rxd || !rx_buf) return 0;
    if (!(rxd[rx_i].sta & 1)) return 0;
    *len = rxd[rx_i].len;
    if (*len > BUF_SZ) *len = BUF_SZ;
    memcpy(buf, rx_buf + (size_t)rx_i * BUF_SZ, *len);
    rxd[rx_i].sta = 0;
    int o = rx_i;
    rx_i = (rx_i+1) % NUM_DESC;
    ew(REG_RDT, o);
    rx_packets++;
    return 1;
}

/* Protocol helpers */
static uint16_t htons(uint16_t v) { return (v>>8)|(v<<8); }

struct eth{uint8_t dst[6],src[6];uint16_t type;} __attribute__((packed));
struct arp{struct eth e;uint16_t hw,pr;uint8_t hl,pl;uint16_t op;
    uint8_t smac[6];uint32_t sip;uint8_t tmac[6];uint32_t tip;} __attribute__((packed));
struct iph{uint8_t vihl,tos;uint16_t tl,id,frag;uint8_t ttl,proto;uint16_t cs;
    uint32_t src,dst;} __attribute__((packed));
struct icmph{uint8_t type,code;uint16_t cs,id,seq;} __attribute__((packed));

static uint8_t gw_mac[6]; static int have_gw=0;

/* ============================================================
 * Minimal TCP client (single connection) + net_poll()
 * ============================================================ */
struct tcph {
    uint16_t sport,dport;
    uint32_t seq,ack;
    uint8_t  off_res;
    uint8_t  flags;
    uint16_t win;
    uint16_t cs;
    uint16_t urg;
} __attribute__((packed));

static uint16_t ip_id = 0x1234;

static uint16_t bswap16(uint16_t v) { return (uint16_t)((v>>8)|(v<<8)); }
static uint32_t bswap32(uint32_t v) {
    return ((v>>24)&0xFF) | ((v>>8)&0xFF00) | ((v<<8)&0xFF0000) | ((v<<24)&0xFF000000);
}
static uint16_t ntohs(uint16_t v) { return bswap16(v); }
static uint32_t ntohl(uint32_t v) { return bswap32(v); }
static uint32_t htonl(uint32_t v) { return bswap32(v); }

static uint16_t cksum_add(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}
static uint16_t cksum16(const void *data, int len) {
    uint32_t sum = 0;
    const uint16_t *p = (const uint16_t *)data;
    /* Internet checksum is defined over big-endian 16-bit words (network order). */
    while (len > 1) { sum += ntohs(*p++); len -= 2; }
    if (len == 1) sum += (uint16_t)(*(const uint8_t *)p) << 8;
    return cksum_add(sum);
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                             const void *tcp_seg, int tcp_len) {
    uint32_t sum = 0;
    /* Pseudo-header: big-endian 16-bit words. */
    sum += (src_ip >> 16) + (src_ip & 0xFFFF);
    sum += (dst_ip >> 16) + (dst_ip & 0xFFFF);
    sum += 6; /* protocol */
    sum += (uint16_t)tcp_len;
    const uint16_t *p = (const uint16_t *)tcp_seg;
    int len = tcp_len;
    while (len > 1) { sum += ntohs(*p++); len -= 2; }
    if (len == 1) sum += (uint16_t)(*(const uint8_t *)p) << 8;
    return cksum_add(sum);
}

/* Forward decl: used by ip_send() before the static definition below. */
static int arp_resolve(uint32_t ip, uint8_t *out, int tmo);

static void ip_send(uint32_t dst_ip, uint8_t proto,
                    const uint8_t *payload, uint16_t pay_len) {
    if (!nic_ok) return;
    uint8_t pkt[sizeof(struct eth) + sizeof(struct iph) + 1500];
    int off = 0;
    struct eth *e = (struct eth*)pkt;
    if (!have_gw) {
        if (arp_resolve(gw_ip, gw_mac, 500) == 0) have_gw = 1;
    }
    if (have_gw) memcpy(e->dst, gw_mac, 6);
    else memset(e->dst, 0xFF, 6); /* best-effort fallback */
    memcpy(e->src, mac, 6);
    e->type = htons(0x0800);
    off += sizeof(struct eth);
    struct iph *ip_h = (struct iph*)(pkt + off);
    memset(ip_h, 0, sizeof(*ip_h));
    ip_h->vihl = 0x45;
    ip_h->ttl = 64;
    ip_h->proto = proto;
    ip_h->src = htonl(our_ip);
    ip_h->dst = htonl(dst_ip);
    ip_h->id = htons(++ip_id);
    ip_h->tl = htons((uint16_t)(sizeof(struct iph) + pay_len));
    ip_h->cs = 0;
    ip_h->cs = cksum16(ip_h, sizeof(struct iph));
    off += sizeof(struct iph);
    memcpy(pkt + off, payload, pay_len);
    off += pay_len;
    e1000_send(pkt, (uint16_t)off);
}

static void arp_reply(const struct arp *req) {
    if (!req) return;
    struct arp r;
    memset(&r, 0, sizeof(r));
    memcpy(r.e.dst, req->e.src, 6);
    memcpy(r.e.src, mac, 6);
    r.e.type = htons(0x0806);
    r.hw = htons(1);
    r.pr = htons(0x0800);
    r.hl = 6;
    r.pl = 4;
    r.op = htons(2);
    memcpy(r.smac, mac, 6);
    r.sip = htonl(our_ip);
    memcpy(r.tmac, req->smac, 6);
    r.tip = req->sip;
    e1000_send((uint8_t*)&r, sizeof(r));
}

enum { TCP_CLOSED=0, TCP_SYN_SENT=1, TCP_EST=2, TCP_FIN_WAIT=3 };
static struct {
    int state;
    uint32_t dst_ip;
    uint16_t sport;
    uint16_t dport;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint8_t  rxbuf[2048];
    int      rxlen;
} tcp0;

static void tcp_reset_state(void) {
    memset(&tcp0, 0, sizeof(tcp0));
    tcp0.state = TCP_CLOSED;
}

static void tcp_send_flags(uint8_t flags, const uint8_t *data, int data_len) {
    uint8_t seg[sizeof(struct tcph) + 1400];
    if (data_len < 0) data_len = 0;
    if (data_len > 1400) data_len = 1400;
    struct tcph *t = (struct tcph*)seg;
    memset(t, 0, sizeof(*t));
    t->sport = htons(tcp0.sport);
    t->dport = htons(tcp0.dport);
    t->seq = htonl(tcp0.snd_nxt);
    t->ack = htonl(tcp0.rcv_nxt);
    t->off_res = (uint8_t)(5 << 4);
    t->flags = flags;
    t->win = htons(4096);
    t->cs = 0;
    if (data_len > 0 && data)
        memcpy(seg + sizeof(struct tcph), data, (size_t)data_len);
    int seg_len = (int)sizeof(struct tcph) + data_len;
    t->cs = tcp_checksum(our_ip, tcp0.dst_ip, seg, seg_len);
    ip_send(tcp0.dst_ip, 6, seg, (uint16_t)seg_len);
    if (flags & 0x02) tcp0.snd_nxt += 1;
    if (flags & 0x01) tcp0.snd_nxt += 1;
    tcp0.snd_nxt += (uint32_t)data_len;
}

static int tcp_connect(uint32_t ip, uint16_t port, int timeout_ms) {
    tcp_reset_state();
    tcp0.dst_ip = ip;
    tcp0.dport = port;
    tcp0.sport = (uint16_t)(40000 + (timer_get_ticks() % 20000));
    tcp0.snd_nxt = (uint32_t)(timer_get_ticks() * 1103515245u + 12345u);
    tcp0.rcv_nxt = 0;
    tcp0.rxlen = 0;
    tcp0.state = TCP_SYN_SENT;
    tcp_send_flags(0x02, 0, 0); /* SYN */

    uint64_t st = timer_get_ticks();
    uint64_t dl = st + (uint64_t)(timeout_ms * 100 / 1000);
    if (dl <= st) dl = st + 10;
    while (timer_get_ticks() < dl) {
        net_poll();
        if (tcp0.state == TCP_EST)
            return 0;
        if (gui_mode_active) gui_pump();
        arch_halt();
    }
    return -1;
}

static int tcp_send_data(const uint8_t *data, int len, int timeout_ms) {
    if (tcp0.state != TCP_EST) return -1;
    if (len < 0) return -1;
    int sent = 0;
    uint64_t st = timer_get_ticks();
    uint64_t dl = st + (uint64_t)(timeout_ms * 100 / 1000);
    if (dl <= st) dl = st + 10;
    while (sent < len && timer_get_ticks() < dl) {
        int chunk = len - sent;
        if (chunk > 800) chunk = 800;
        tcp_send_flags(0x18, data + sent, chunk); /* PSH|ACK */
        sent += chunk;
        net_poll();
        if (gui_mode_active) gui_pump();
    }
    return (sent == len) ? 0 : -1;
}

static int tcp_recv_some(uint8_t *out, int out_sz, int timeout_ms) {
    if (tcp0.state != TCP_EST && tcp0.state != TCP_FIN_WAIT) return -1;
    uint64_t st = timer_get_ticks();
    uint64_t dl = st + (uint64_t)(timeout_ms * 100 / 1000);
    if (dl <= st) dl = st + 10;
    while (timer_get_ticks() < dl) {
        net_poll();
        if (tcp0.rxlen > 0) {
            int n = tcp0.rxlen;
            if (n > out_sz) n = out_sz;
            memcpy(out, tcp0.rxbuf, (size_t)n);
            memmove(tcp0.rxbuf, tcp0.rxbuf + n, (size_t)(tcp0.rxlen - n));
            tcp0.rxlen -= n;
            return n;
        }
        if (tcp0.state == TCP_FIN_WAIT)
            return 0;
        if (gui_mode_active) gui_pump();
        arch_halt();
    }
    return 0;
}

static void tcp_close(void) {
    if (tcp0.state == TCP_EST) {
        tcp_send_flags(0x11, 0, 0); /* FIN|ACK */
        tcp0.state = TCP_FIN_WAIT;
    }
}

void net_poll(void) {
    if (!nic_ok) return;
    uint8_t b[BUF_SZ];
    uint16_t l = 0;
    while (e1000_recv(b, &l)) {
        if (l < sizeof(struct eth))
            continue;
        struct eth *e = (struct eth*)b;
        uint16_t et = ntohs(e->type);
        if (et == 0x0806) {
            if (l < sizeof(struct arp))
                continue;
            struct arp *a = (struct arp*)b;
            uint16_t op = ntohs(a->op);
            if (op == 2) {
                /* Learn gateway MAC from ARP replies (QEMU usernet uses 10.0.2.2). */
                if (ntohl(a->sip) == gw_ip) {
                    memcpy(gw_mac, a->smac, 6);
                    have_gw = 1;
                }
            }
            if (op == 1 && ntohl(a->tip) == our_ip) {
                arp_reply(a);
            }
        } else if (et == 0x0800) {
            if (l < sizeof(struct eth) + sizeof(struct iph))
                continue;
            struct iph *ip_h = (struct iph*)(b + sizeof(struct eth));
            if ((ip_h->vihl >> 4) != 4)
                continue;
            int ihl = (ip_h->vihl & 0x0F) * 4;
            if (ihl < (int)sizeof(struct iph))
                continue;
            if (l < (uint16_t)(sizeof(struct eth) + ihl))
                continue;
            uint16_t tot = ntohs(ip_h->tl);
            if (tot < (uint16_t)ihl)
                continue;
            if (ntohl(ip_h->dst) != our_ip)
                continue;
            if (ip_h->proto == 6) {
                int ip_off = (int)sizeof(struct eth) + ihl;
                if (l < (uint16_t)(ip_off + (int)sizeof(struct tcph)))
                    continue;
                struct tcph *t = (struct tcph*)(b + ip_off);
                uint16_t dport = ntohs(t->dport);
                uint16_t sport = ntohs(t->sport);
                uint32_t seq = ntohl(t->seq);
                int thl = ((t->off_res >> 4) & 0x0F) * 4;
                if (thl < (int)sizeof(struct tcph))
                    continue;
                int tcp_off = ip_off + thl;
                int pay_len = (int)tot - ihl - thl;
                if (pay_len < 0)
                    pay_len = 0;
                if (tcp_off + pay_len > (int)l)
                    pay_len = (int)l - tcp_off;

                if (tcp0.state != TCP_CLOSED &&
                    ntohl(ip_h->src) == tcp0.dst_ip &&
                    dport == tcp0.sport &&
                    sport == tcp0.dport) {
                    uint8_t flags = t->flags;
                    if (tcp0.state == TCP_SYN_SENT) {
                        if ((flags & 0x12) == 0x12) {
                            tcp0.rcv_nxt = seq + 1;
                            tcp0.state = TCP_EST;
                            tcp_send_flags(0x10, 0, 0); /* ACK */
                        }
                        continue;
                    }
                    if (tcp0.state == TCP_EST || tcp0.state == TCP_FIN_WAIT) {
                        if (pay_len > 0 && seq == tcp0.rcv_nxt) {
                            int space = (int)sizeof(tcp0.rxbuf) - tcp0.rxlen;
                            if (space > 0) {
                                int n = pay_len;
                                if (n > space) n = space;
                                memcpy(tcp0.rxbuf + tcp0.rxlen, b + tcp_off, (size_t)n);
                                tcp0.rxlen += n;
                            }
                            tcp0.rcv_nxt += (uint32_t)pay_len;
                            tcp_send_flags(0x10, 0, 0);
                        } else if (flags & 0x01) {
                            if (seq == tcp0.rcv_nxt) {
                                tcp0.rcv_nxt += 1;
                                tcp_send_flags(0x10, 0, 0);
                                tcp0.state = TCP_FIN_WAIT;
                            }
                        } else {
                            tcp_send_flags(0x10, 0, 0);
                        }
                    }
                }
            }
        }
    }
}

static int arp_resolve(uint32_t ip, uint8_t *out, int tmo) {
    if (ip == gw_ip && have_gw) {
        if (out) memcpy(out, gw_mac, 6);
        return 0;
    }
    struct arp p; memset(&p,0,sizeof(p));
    memset(p.e.dst,0xFF,6); memcpy(p.e.src,mac,6); p.e.type=htons(0x0806);
    p.hw=htons(1); p.pr=htons(0x0800); p.hl=6; p.pl=4; p.op=htons(1);
    memcpy(p.smac,mac,6); p.sip=htonl(our_ip); p.tip=htonl(ip);
    e1000_send((uint8_t*)&p,sizeof(p));
    uint64_t dl=timer_get_ticks()+(tmo*100/1000);
    while(timer_get_ticks()<dl) {
        net_poll();
        if (ip == gw_ip && have_gw) {
            if (out) memcpy(out, gw_mac, 6);
            return 0;
        }
        if (gui_mode_active) gui_pump();
        arch_halt();
    }
    return -1;
}

/* ============================================================
 * Public API
 * ============================================================ */
int net_init(void) {
    our_ip=net_make_ip(10,0,2,15); gw_ip=net_make_ip(10,0,2,2);
    /* Only scan bus 0 (safer, faster) */
    for(int d=0;d<32;d++) {
        uint32_t id=pci_config_read(0,d,0,0);
        if((id&0xFFFF)==E1000_VID && ((id>>16)&0xFFFF)==E1000_DID) {
            uint32_t bar0=pci_config_read(0,d,0,0x10);
            if(bar0&1) continue; /* I/O BAR, skip */
            mmio=(volatile uint32_t*)(uintptr_t)((bar0&~0xFu) + hhdm_offset);

            /* Enable bus mastering + memory space access */
            uint32_t cmd=pci_config_read(0,d,0,4);
            pci_config_write(0,d,0,4,cmd|(1<<2)|(1<<1));

            /* ---- Software Reset ---- */
            uint32_t ctrl = er(REG_CTRL);
            ew(REG_CTRL, ctrl | CTRL_RST);
            /* Wait for reset to complete (~1ms is enough) */
            for (volatile int w=0; w<100000; w++);
            /* Clear the reset bit (it auto-clears on real HW, but be safe) */

            /* Disable all interrupts (we poll) */
            ew(REG_IMC, 0xFFFFFFFF);
            /* Clear pending interrupts */
            er(REG_ICR);

            /* ---- Set Link Up ---- */
            ctrl = er(REG_CTRL);
            ew(REG_CTRL, (ctrl | CTRL_SLU | CTRL_ASDE) & ~CTRL_RST);

            /* Wait for link up (up to ~500ms) */
            int link_up = 0;
            for (int w=0; w<50000; w++) {
                if (er(REG_STATUS) & STATUS_LU) { link_up=1; break; }
                for (volatile int j=0; j<100; j++);
            }
            if (!link_up) continue; /* No link, skip */

            /* Read MAC */
            uint32_t lo=er(REG_RAL),hi=er(REG_RAH);
            mac[0]=lo;mac[1]=lo>>8;mac[2]=lo>>16;mac[3]=lo>>24;mac[4]=hi;mac[5]=hi>>8;
            if(!mac[0]&&!mac[1]){mac[0]=0x52;mac[1]=0x54;mac[2]=0;mac[3]=0x12;mac[4]=0x34;mac[5]=0x56;}
            /* Program MAC filter (RAH bit 31 = Address Valid). */
            uint32_t ral = (uint32_t)mac[0] | ((uint32_t)mac[1] << 8) |
                           ((uint32_t)mac[2] << 16) | ((uint32_t)mac[3] << 24);
            uint32_t rah = (uint32_t)mac[4] | ((uint32_t)mac[5] << 8) | (1u << 31);
            ew(REG_RAL, ral);
            ew(REG_RAH, rah);
            /* Clear multicast table array. */
            for (int i = 0; i < 128; i++) ew(REG_MTA + (uint32_t)(i * 4), 0);

            /* Allocate rings/buffers from usable physical memory (HHDM). */
            uint64_t txd_phys = 0, rxd_phys = 0;
            txd = (struct tx_desc *)net_pmm_alloc(NUM_DESC * sizeof(struct tx_desc), 128, &txd_phys);
            rxd = (struct rx_desc *)net_pmm_alloc(NUM_DESC * sizeof(struct rx_desc), 128, &rxd_phys);
            tx_buf = (uint8_t *)net_pmm_alloc(NUM_DESC * BUF_SZ, 16, 0);
            rx_buf = (uint8_t *)net_pmm_alloc(NUM_DESC * BUF_SZ, 16, 0);
            if (!txd || !rxd || !tx_buf || !rx_buf) {
                txd = 0; rxd = 0; tx_buf = 0; rx_buf = 0;
                continue;
            }

            /* ---- Init TX ---- */
            memset(txd,0,NUM_DESC * sizeof(struct tx_desc));
            for(int i=0;i<NUM_DESC;i++){
                uint8_t *b = tx_buf + (size_t)i * BUF_SZ;
                txd[i].addr=dma_phys(b);
                txd[i].sta=1;
            }
            ew(REG_TDBAL,(uint32_t)txd_phys);ew(REG_TDBAL+4,(uint32_t)(txd_phys>>32));
            ew(REG_TDLEN,NUM_DESC*sizeof(struct tx_desc));ew(REG_TDH,0);ew(REG_TDT,0);
            ew(REG_TCTL,(1<<1)|(1<<3)|(15<<4)|(64<<12));

            /* ---- Init RX ---- */
            memset(rxd,0,NUM_DESC * sizeof(struct rx_desc));
            for(int i=0;i<NUM_DESC;i++) {
                uint8_t *b = rx_buf + (size_t)i * BUF_SZ;
                rxd[i].addr=dma_phys(b);
            }
            ew(REG_RDBAL,(uint32_t)rxd_phys);ew(REG_RDBAL+4,(uint32_t)(rxd_phys>>32));
            ew(REG_RDLEN,NUM_DESC*sizeof(struct rx_desc));ew(REG_RDH,0);ew(REG_RDT,NUM_DESC-1);
            /* RCTL: EN | UPE | MPE | BAM | SECRC */
            ew(REG_RCTL,(1<<1)|(1<<3)|(1<<4)|(1<<15)|(1<<26));
            /* Some e1000 variants expect RDT to be set after enabling RCTL. */
            ew(REG_RDH, 0);
            ew(REG_RDT, NUM_DESC - 1);

            nic_ok=1; tx_i=0; rx_i=0;
            return 0;
        }
    }
    return -1;
}

int net_is_available(void) { return nic_ok; }
void net_get_packet_counts(uint32_t *txp, uint32_t *rxp) {
    if (txp) *txp = tx_packets;
    if (rxp) *rxp = rx_packets;
}
void net_get_e1000_debug(uint32_t *status, uint32_t *rctl, uint32_t *rdh, uint32_t *rdt) {
    if (!nic_ok || !mmio) {
        if (status) *status = 0;
        if (rctl) *rctl = 0;
        if (rdh) *rdh = 0;
        if (rdt) *rdt = 0;
        return;
    }
    if (status) *status = er(REG_STATUS);
    if (rctl) *rctl = er(REG_RCTL);
    if (rdh) *rdh = er(REG_RDH);
    if (rdt) *rdt = er(REG_RDT);
}
uint32_t net_make_ip(uint8_t a,uint8_t b,uint8_t c,uint8_t d) {
    /* Host-order numeric representation (e.g. 10.0.2.15 => 0x0A00020F). */
    return ((uint32_t)a<<24)|((uint32_t)b<<16)|((uint32_t)c<<8)|((uint32_t)d);
}
void net_format_ip(uint32_t ip, char *buf) {
    char t[4]; int p=0;
    for(int i=0;i<4;i++){int_to_str((ip>>(24-(i*8)))&0xFF,t);
        for(int j=0;t[j];j++) buf[p++]=t[j];
        if(i<3) buf[p++]='.';
    }
    buf[p]=0;
}

int net_ping(uint32_t ip, int timeout_ms) {
    if(!nic_ok) return -1;
    /* ARP resolve gateway (500ms timeout — short so it doesn't freeze) */
    if(!have_gw){if(arp_resolve(gw_ip,gw_mac,500)==0)have_gw=1;else return -1;}
    static uint16_t seq=0; seq++;
    uint8_t pkt[sizeof(struct eth)+sizeof(struct iph)+sizeof(struct icmph)+32];
    memset(pkt,0,sizeof(pkt)); int off=0;
    struct eth *e=(struct eth*)pkt;
    memcpy(e->dst,gw_mac,6);memcpy(e->src,mac,6);e->type=htons(0x0800);off+=sizeof(struct eth);
    struct iph *ip_h=(struct iph*)(pkt+off);
    ip_h->vihl=0x45;ip_h->ttl=64;ip_h->proto=1;ip_h->src=htonl(our_ip);ip_h->dst=htonl(ip);
    ip_h->tl=htons(sizeof(struct iph)+sizeof(struct icmph)+32);
    ip_h->cs=0;ip_h->cs=cksum16(ip_h,sizeof(struct iph));off+=sizeof(struct iph);
    struct icmph *ic=(struct icmph*)(pkt+off);
    ic->type=8;ic->id=htons(0x1234);ic->seq=htons(seq);off+=sizeof(struct icmph);
    for(int i=0;i<32;i++) { pkt[off+i]=(uint8_t)i; } off+=32;
    ic->cs=0;ic->cs=cksum16(ic,sizeof(struct icmph)+32);
    uint64_t st=timer_get_ticks();
    e1000_send(pkt,off);
    uint64_t dl=st+(timeout_ms*100/1000);
    uint8_t rb[BUF_SZ]; uint16_t rl;
    while(timer_get_ticks()<dl) {
        if(e1000_recv(rb,&rl)&&rl>sizeof(struct eth)+sizeof(struct iph)+sizeof(struct icmph)) {
            struct eth *re=(struct eth*)rb;
            if(ntohs(re->type)!=0x0800) goto next;
            struct iph *ri=(struct iph*)(rb+sizeof(struct eth));
            if(ri->proto!=1) goto next;
            if (ntohl(ri->dst) != our_ip) goto next;
            struct icmph *rc=(struct icmph*)(rb+sizeof(struct eth)+sizeof(struct iph));
            if(rc->type==0 && ntohs(rc->id)==0x1234 && ntohs(rc->seq)==seq)
                return (int)((timer_get_ticks()-st)*10);
        }
        next:
        if (gui_mode_active) gui_pump();
        arch_halt();
    }
    return -1;
}

int net_http_get(uint32_t ip, int port, const char *host, const char *path,
                 char *out, int out_sz, int timeout_ms) {
    if (!out || out_sz <= 0)
        return -1;
    out[0] = '\0';
    if (!nic_ok)
        return -1;
    if (!host) host = "";
    if (!path || !path[0]) path = "/";
    if (port <= 0) port = 80;

    if (tcp_connect(ip, (uint16_t)port, timeout_ms) != 0)
        return -1;

    char req[512];
    memset(req, 0, sizeof(req));
    int rp = 0;
    const char *a1 = "GET ";
    for (int i = 0; a1[i] && rp < (int)sizeof(req) - 1; i++) req[rp++] = a1[i];
    for (int i = 0; path[i] && rp < (int)sizeof(req) - 1; i++) req[rp++] = path[i];
    const char *a2 = " HTTP/1.0\r\nHost: ";
    for (int i = 0; a2[i] && rp < (int)sizeof(req) - 1; i++) req[rp++] = a2[i];
    for (int i = 0; host[i] && rp < (int)sizeof(req) - 1; i++) req[rp++] = host[i];
    const char *a3 = "\r\nUser-Agent: akaOS\r\nConnection: close\r\n\r\n";
    for (int i = 0; a3[i] && rp < (int)sizeof(req) - 1; i++) req[rp++] = a3[i];
    req[rp] = '\0';

    if (tcp_send_data((const uint8_t*)req, (int)strlen(req), timeout_ms) != 0) {
        tcp_close();
        return -1;
    }

    int total = 0;
    uint8_t tmp[512];
    uint64_t st = timer_get_ticks();
    uint64_t dl = st + (uint64_t)(timeout_ms * 100 / 1000);
    if (dl <= st) dl = st + 50;
    while (timer_get_ticks() < dl && total < out_sz - 1) {
        int n = tcp_recv_some(tmp, (int)sizeof(tmp), 200);
        if (n < 0)
            break;
        if (n == 0) {
            if (tcp0.state == TCP_FIN_WAIT)
                break;
            continue;
        }
        int copy = n;
        if (copy > out_sz - 1 - total)
            copy = out_sz - 1 - total;
        memcpy(out + total, tmp, (size_t)copy);
        total += copy;
        out[total] = '\0';
    }
    tcp_close();
    return total;
}
