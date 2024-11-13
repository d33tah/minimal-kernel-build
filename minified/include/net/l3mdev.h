/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * include/net/l3mdev.h - L3 master device API
 * Copyright (c) 2015 Cumulus Networks
 * Copyright (c) 2015 David Ahern <dsa@cumulusnetworks.com>
 */
#ifndef _NET_L3MDEV_H_
#define _NET_L3MDEV_H_

#include <net/dst.h>
#include <net/fib_rules.h>

enum l3mdev_type {
	L3MDEV_TYPE_UNSPEC,
	L3MDEV_TYPE_VRF,
	__L3MDEV_TYPE_MAX
};

#define L3MDEV_TYPE_MAX (__L3MDEV_TYPE_MAX - 1)

typedef int (*lookup_by_table_id_t)(struct net *net, u32 table_d);

/**
 * struct l3mdev_ops - l3mdev operations
 *
 * @l3mdev_fib_table: Get FIB table id to use for lookups
 *
 * @l3mdev_l3_rcv:    Hook in L3 receive path
 *
 * @l3mdev_l3_out:    Hook in L3 output path
 *
 * @l3mdev_link_scope_lookup: IPv6 lookup for linklocal and mcast destinations
 */

struct l3mdev_ops {
	u32		(*l3mdev_fib_table)(const struct net_device *dev);
	struct sk_buff * (*l3mdev_l3_rcv)(struct net_device *dev,
					  struct sk_buff *skb, u16 proto);
	struct sk_buff * (*l3mdev_l3_out)(struct net_device *dev,
					  struct sock *sk, struct sk_buff *skb,
					  u16 proto);

	/* IPv6 ops */
	struct dst_entry * (*l3mdev_link_scope_lookup)(const struct net_device *dev,
						 struct flowi6 *fl6);
};


static inline int l3mdev_master_ifindex_rcu(const struct net_device *dev)
{
	return 0;
}
static inline int l3mdev_master_ifindex(struct net_device *dev)
{
	return 0;
}

static inline int l3mdev_master_ifindex_by_index(struct net *net, int ifindex)
{
	return 0;
}

static inline
int l3mdev_master_upper_ifindex_by_index_rcu(struct net *net, int ifindex)
{
	return 0;
}
static inline
int l3mdev_master_upper_ifindex_by_index(struct net *net, int ifindex)
{
	return 0;
}

static inline
struct net_device *l3mdev_master_dev_rcu(const struct net_device *dev)
{
	return NULL;
}

static inline u32 l3mdev_fib_table_rcu(const struct net_device *dev)
{
	return 0;
}
static inline u32 l3mdev_fib_table(const struct net_device *dev)
{
	return 0;
}
static inline u32 l3mdev_fib_table_by_index(struct net *net, int ifindex)
{
	return 0;
}

static inline bool netif_index_is_l3_master(struct net *net, int ifindex)
{
	return false;
}

static inline
struct dst_entry *l3mdev_link_scope_lookup(struct net *net, struct flowi6 *fl6)
{
	return NULL;
}

static inline
struct sk_buff *l3mdev_ip_rcv(struct sk_buff *skb)
{
	return skb;
}

static inline
struct sk_buff *l3mdev_ip6_rcv(struct sk_buff *skb)
{
	return skb;
}

static inline
struct sk_buff *l3mdev_ip_out(struct sock *sk, struct sk_buff *skb)
{
	return skb;
}

static inline
struct sk_buff *l3mdev_ip6_out(struct sock *sk, struct sk_buff *skb)
{
	return skb;
}

static inline
int l3mdev_table_lookup_register(enum l3mdev_type l3type,
				 lookup_by_table_id_t fn)
{
	return -EOPNOTSUPP;
}

static inline
void l3mdev_table_lookup_unregister(enum l3mdev_type l3type,
				    lookup_by_table_id_t fn)
{
}

static inline
int l3mdev_ifindex_lookup_by_table_id(enum l3mdev_type l3type, struct net *net,
				      u32 table_id)
{
	return -ENODEV;
}

static inline
int l3mdev_fib_rule_match(struct net *net, struct flowi *fl,
			  struct fib_lookup_arg *arg)
{
	return 1;
}
static inline
void l3mdev_update_flow(struct net *net, struct flowi *fl)
{
}

#endif /* _NET_L3MDEV_H_ */
