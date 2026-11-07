#ifndef _SHIM_LINUX_NET_NET_H
#define _SHIM_LINUX_NET_NET_H

#include <shim/linux/types.h>

struct net_device;

int register_netdevice(struct net_device *dev);
void unregister_netdevice(struct net_device *dev);

#endif /* _SHIM_LINUX_NET_NET_H */
