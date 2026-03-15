/* ============================================================
 * akaOS — Network Stack (PCI + e1000 + ARP/IP/ICMP)
 * ============================================================ */
#include "net.h"
#include "io.h"
#include "string.h"
#include "time.h"

extern void gui_pump(void);
extern volatile int gui_mode_active;
extern uint64_t hhdm_offset;

/* ============================================================
 * PCI — proper 32-bit I/O
 * ============================================================ */
static void pci_write32(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
static uint32_t pci_read32(uint16_t port) {
    uint32_t val;
    asm volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
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

static struct tx_desc txd[NUM_DESC] __attribute__((aligned(128)));
static struct rx_desc rxd[NUM_DESC] __attribute__((aligned(128)));
static uint8_t tx_buf[NUM_DESC][BUF_SZ] __attribute__((aligned(16)));
static uint8_t rx_buf[NUM_DESC][BUF_SZ] __attribute__((aligned(16)));

static volatile uint32_t *mmio = 0;
static uint8_t mac[6];
static int nic_ok = 0;
static int tx_i = 0, rx_i = 0;
static uint32_t our_ip, gw_ip;

static void ew(uint32_t r, uint32_t v) { mmio[r/4]=v; }
static uint32_t er(uint32_t r) { return mmio[r/4]; }

static void e1000_send(uint8_t *data, uint16_t len) {
    if (!nic_ok) return;
    memcpy(tx_buf[tx_i], data, len);
    txd[tx_i].addr = (uint64_t)(uintptr_t)tx_buf[tx_i];
    txd[tx_i].len = len;
    txd[tx_i].cmd = 0x0B; /* EOP + IFCS + RS */
    txd[tx_i].sta = 0;
    int o = tx_i;
    tx_i = (tx_i+1) % NUM_DESC;
    ew(REG_TDT, tx_i);
    /* Wait for TX complete (with timeout) */
    for (int t=0; t<500000 && !(txd[o].sta&1); t++) {
        asm volatile("pause");
    }
}

static int e1000_recv(uint8_t *buf, uint16_t *len) {
    if (!nic_ok) return 0;
    if (!(rxd[rx_i].sta & 1)) return 0;
    *len = rxd[rx_i].len;
    if (*len > BUF_SZ) *len = BUF_SZ;
    memcpy(buf, rx_buf[rx_i], *len);
    rxd[rx_i].sta = 0;
    int o = rx_i;
    rx_i = (rx_i+1) % NUM_DESC;
    ew(REG_RDT, o);
    return 1;
}

/* Protocol helpers */
static uint16_t htons(uint16_t v) { return (v>>8)|(v<<8); }
static uint16_t cksum(void *d, int n) {
    uint32_t s=0; uint16_t *p=d;
    while(n>1){s+=*p++;n-=2;} if(n)s+=*(uint8_t*)p;
    while(s>>16) { s=(s&0xFFFF)+(s>>16); } return ~s;
}

struct eth{uint8_t dst[6],src[6];uint16_t type;} __attribute__((packed));
struct arp{struct eth e;uint16_t hw,pr;uint8_t hl,pl;uint16_t op;
    uint8_t smac[6];uint32_t sip;uint8_t tmac[6];uint32_t tip;} __attribute__((packed));
struct iph{uint8_t vihl,tos;uint16_t tl,id,frag;uint8_t ttl,proto;uint16_t cs;
    uint32_t src,dst;} __attribute__((packed));
struct icmph{uint8_t type,code;uint16_t cs,id,seq;} __attribute__((packed));

static uint8_t gw_mac[6]; static int have_gw=0;

static int arp_resolve(uint32_t ip, uint8_t *out, int tmo) {
    struct arp p; memset(&p,0,sizeof(p));
    memset(p.e.dst,0xFF,6); memcpy(p.e.src,mac,6); p.e.type=htons(0x0806);
    p.hw=htons(1); p.pr=htons(0x0800); p.hl=6; p.pl=4; p.op=htons(1);
    memcpy(p.smac,mac,6); p.sip=our_ip; p.tip=ip;
    e1000_send((uint8_t*)&p,sizeof(p));
    uint64_t dl=timer_get_ticks()+(tmo*100/1000);
    uint8_t b[BUF_SZ]; uint16_t l;
    while(timer_get_ticks()<dl) {
        if(e1000_recv(b,&l)&&l>=sizeof(struct arp)) {
            struct arp *r=(struct arp*)b;
            if(htons(r->e.type)==0x0806&&htons(r->op)==2&&r->sip==ip)
                {memcpy(out,r->smac,6);return 0;}
        }
        if (gui_mode_active) gui_pump();
        asm volatile("hlt");
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

            /* ---- Init TX ---- */
            memset(txd,0,sizeof(txd));
            for(int i=0;i<NUM_DESC;i++){txd[i].addr=(uint64_t)(uintptr_t)tx_buf[i];txd[i].sta=1;}
            ew(REG_TDBAL,(uint32_t)(uintptr_t)txd);ew(REG_TDBAL+4,0);
            ew(REG_TDLEN,NUM_DESC*sizeof(struct tx_desc));ew(REG_TDH,0);ew(REG_TDT,0);
            ew(REG_TCTL,(1<<1)|(1<<3)|(15<<4)|(64<<12));

            /* ---- Init RX ---- */
            memset(rxd,0,sizeof(rxd));
            for(int i=0;i<NUM_DESC;i++) rxd[i].addr=(uint64_t)(uintptr_t)rx_buf[i];
            ew(REG_RDBAL,(uint32_t)(uintptr_t)rxd);ew(REG_RDBAL+4,0);
            ew(REG_RDLEN,NUM_DESC*sizeof(struct rx_desc));ew(REG_RDH,0);ew(REG_RDT,NUM_DESC-1);
            ew(REG_RCTL,(1<<1)|(1<<5)|(1<<15)|(1<<26));

            nic_ok=1; tx_i=0; rx_i=0;
            return 0;
        }
    }
    return -1;
}

int net_is_available(void) { return nic_ok; }
uint32_t net_make_ip(uint8_t a,uint8_t b,uint8_t c,uint8_t d) {
    return (uint32_t)a|((uint32_t)b<<8)|((uint32_t)c<<16)|((uint32_t)d<<24);
}
void net_format_ip(uint32_t ip, char *buf) {
    char t[4]; int p=0;
    for(int i=0;i<4;i++){int_to_str((ip>>(i*8))&0xFF,t);
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
    ip_h->vihl=0x45;ip_h->ttl=64;ip_h->proto=1;ip_h->src=our_ip;ip_h->dst=ip;
    ip_h->tl=htons(sizeof(struct iph)+sizeof(struct icmph)+32);
    ip_h->cs=0;ip_h->cs=cksum(ip_h,sizeof(struct iph));off+=sizeof(struct iph);
    struct icmph *ic=(struct icmph*)(pkt+off);
    ic->type=8;ic->id=htons(0x1234);ic->seq=htons(seq);off+=sizeof(struct icmph);
    for(int i=0;i<32;i++) { pkt[off+i]=(uint8_t)i; } off+=32;
    ic->cs=0;ic->cs=cksum(ic,sizeof(struct icmph)+32);
    uint64_t st=timer_get_ticks();
    e1000_send(pkt,off);
    uint64_t dl=st+(timeout_ms*100/1000);
    uint8_t rb[BUF_SZ]; uint16_t rl;
    while(timer_get_ticks()<dl) {
        if(e1000_recv(rb,&rl)&&rl>sizeof(struct eth)+sizeof(struct iph)+sizeof(struct icmph)) {
            struct eth *re=(struct eth*)rb;
            if(htons(re->type)!=0x0800) goto next;
            struct iph *ri=(struct iph*)(rb+sizeof(struct eth));
            if(ri->proto!=1) goto next;
            struct icmph *rc=(struct icmph*)(rb+sizeof(struct eth)+sizeof(struct iph));
            if(rc->type==0&&htons(rc->seq)==seq)
                return (int)((timer_get_ticks()-st)*10);
        }
        next:
        if (gui_mode_active) gui_pump();
        asm volatile("hlt");
    }
    return -1;
}
