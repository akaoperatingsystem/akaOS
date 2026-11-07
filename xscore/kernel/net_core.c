#include <shim/linux/netdevice.h>
#include <shim/linux/skbuff.h>
#include <xscore/net.h>
#include <xscore/serial.h>
#include <xscore/memory.h>

#define MAX_NET_DEVICES 8
static struct net_device *g_net_devices[MAX_NET_DEVICES];
static uint32_t g_net_device_count = 0;

int register_netdev(struct net_device *dev) {
    if (!dev || g_net_device_count >= MAX_NET_DEVICES) return -1;
    g_net_devices[g_net_device_count++] = dev;
    xscore_serial_write("[INFO] Registered Linux-compatible network device: ");
    xscore_serial_write(dev->name);
    xscore_serial_write("\n");
    if (dev->netdev_ops && dev->netdev_ops->ndo_open) {
        dev->netdev_ops->ndo_open(dev);
    }
    return 0;
}

void unregister_netdev(struct net_device *dev) {
    for (uint32_t i = 0; i < g_net_device_count; i++) {
        if (g_net_devices[i] == dev) {
            if (dev->netdev_ops && dev->netdev_ops->ndo_stop) {
                dev->netdev_ops->ndo_stop(dev);
            }
            g_net_devices[i] = g_net_devices[--g_net_device_count];
            return;
        }
    }
}

int netif_rx(struct sk_buff *skb) {
    if (!skb || !skb->data || skb->len == 0) return -1;
    xscore_net_queue_rx(skb->data, skb->len);
    if (skb->dev) {
        skb->dev->stats.rx_packets++;
        skb->dev->stats.rx_bytes += skb->len;
    }
    return 0;
}
