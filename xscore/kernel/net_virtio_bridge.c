#include <shim/linux/netdevice.h>
#include <shim/linux/skbuff.h>
#include <xscore/net.h>
#include <xscore/serial.h>

static int virtio_net_open(struct net_device *dev) {
    (void)dev;
    xscore_serial_write("[INFO] Linux netdevice: virtio-net eth0 opened\n");
    return 0;
}

static int virtio_net_stop(struct net_device *dev) {
    (void)dev;
    xscore_serial_write("[INFO] Linux netdevice: virtio-net eth0 stopped\n");
    return 0;
}

static int virtio_net_start_xmit(struct sk_buff *skb, struct net_device *dev) {
    if (!skb || !skb->data || skb->len == 0) return NETDEV_TX_OK;
    int64_t ret = xscore_virtio_net_transmit(skb->data, skb->len);
    if (ret > 0 && dev) {
        dev->stats.tx_packets++;
        dev->stats.tx_bytes += skb->len;
    }
    return NETDEV_TX_OK;
}

static const struct net_device_ops virtio_netdev_ops = {
    .ndo_open = virtio_net_open,
    .ndo_stop = virtio_net_stop,
    .ndo_start_xmit = virtio_net_start_xmit,
};

static struct net_device g_virtio_netdev;

void xscore_linux_net_bridge_init(void) {
    for (size_t i = 0; i < sizeof(g_virtio_netdev); ++i) {
        ((uint8_t *)&g_virtio_netdev)[i] = 0;
    }
    const char name[] = "eth0";
    for (size_t i = 0; i < sizeof(name); ++i) g_virtio_netdev.name[i] = name[i];
    g_virtio_netdev.mtu = 1500;
    g_virtio_netdev.netdev_ops = &virtio_netdev_ops;

    if (xscore_virtio_net_present()) {
        xscore_virtio_net_get_mac(g_virtio_netdev.dev_addr);
    } else {
        g_virtio_netdev.dev_addr[0] = 0x52;
        g_virtio_netdev.dev_addr[1] = 0x54;
        g_virtio_netdev.dev_addr[2] = 0x00;
        g_virtio_netdev.dev_addr[3] = 0x12;
        g_virtio_netdev.dev_addr[4] = 0x34;
        g_virtio_netdev.dev_addr[5] = 0x56;
    }
    register_netdev(&g_virtio_netdev);
}
