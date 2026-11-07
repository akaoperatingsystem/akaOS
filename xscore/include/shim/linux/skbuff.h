#ifndef _SHIM_LINUX_SKBUFF_H
#define _SHIM_LINUX_SKBUFF_H

#include "types.h"

struct net_device;

/*
 * Zero-copy lockless packet buffer abstraction matching Linux sk_buff interface.
 * Directly wraps contiguous physical page frames allocated via XSCore PMM.
 */
struct sk_buff {
    struct sk_buff *next;
    struct sk_buff *prev;
    struct net_device *dev;

    uint8_t *head;
    uint8_t *data;
    uint8_t *tail;
    uint8_t *end;

    unsigned int len;
    unsigned int data_len;
    uint32_t truesize;

    uint16_t protocol;
    uint16_t transport_header;
    uint16_t network_header;
    uint16_t mac_header;

    uint64_t physical_address;
};

static inline unsigned char *skb_put(struct sk_buff *skb, unsigned int len) {
    unsigned char *tmp = skb->tail;
    skb->tail += len;
    skb->len += len;
    return tmp;
}

static inline void skb_reserve(struct sk_buff *skb, unsigned int len) {
    skb->data += len;
    skb->tail += len;
}

static inline unsigned char *skb_push(struct sk_buff *skb, unsigned int len) {
    skb->data -= len;
    skb->len += len;
    return skb->data;
}

static inline unsigned char *skb_pull(struct sk_buff *skb, unsigned int len) {
    if (len > skb->len) return NULL;
    skb->len -= len;
    return skb->data += len;
}

static inline unsigned int skb_headroom(const struct sk_buff *skb) {
    return (unsigned int)(skb->data - skb->head);
}

static inline unsigned int skb_tailroom(const struct sk_buff *skb) {
    return (unsigned int)(skb->end - skb->tail);
}

#endif /* _SHIM_LINUX_SKBUFF_H */
