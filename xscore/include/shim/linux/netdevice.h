#ifndef _SHIM_LINUX_NETDEVICE_H
#define _SHIM_LINUX_NETDEVICE_H

#include "types.h"
#include "skbuff.h"

#define IFNAMSIZ 16
#define ETH_ALEN 6
#define ETH_HLEN 14
#define ETH_DATA_LEN 1500
#define ETH_FRAME_LEN 1514

#define NETDEV_TX_OK 0
#define NETDEV_TX_BUSY 1

struct net_device_stats {
    unsigned long rx_packets;
    unsigned long tx_packets;
    unsigned long rx_bytes;
    unsigned long tx_bytes;
    unsigned long rx_errors;
    unsigned long tx_errors;
    unsigned long rx_dropped;
    unsigned long tx_dropped;
};

struct net_device_ops {
    int (*ndo_open)(struct net_device *dev);
    int (*ndo_stop)(struct net_device *dev);
    int (*ndo_start_xmit)(struct sk_buff *skb, struct net_device *dev);
    struct net_device_stats *(*ndo_get_stats)(struct net_device *dev);
};

struct net_device {
    char name[IFNAMSIZ];
    unsigned char dev_addr[ETH_ALEN];
    unsigned char broadcast[ETH_ALEN];
    unsigned int flags;
    unsigned int mtu;

    const struct net_device_ops *netdev_ops;
    struct net_device_stats stats;
    void *priv;
};

int register_netdev(struct net_device *dev);
void unregister_netdev(struct net_device *dev);
int netif_rx(struct sk_buff *skb);

#endif /* _SHIM_LINUX_NETDEVICE_H */
