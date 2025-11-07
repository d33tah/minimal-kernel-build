/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Operations on the network namespace - minimal stub for minimal kernel
 */
#ifndef __NET_NET_NAMESPACE_H
#define __NET_NET_NAMESPACE_H

#include <linux/ns_common.h>

struct net {
	struct ns_common ns;
};

static inline struct net *get_net(struct net *net)
{
	return net;
}

static inline void put_net(struct net *net)
{
}

extern struct net init_net;

#endif /* __NET_NET_NAMESPACE_H */