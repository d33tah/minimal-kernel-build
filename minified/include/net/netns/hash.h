/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Network namespace hash operations - minimal stub
 */
#ifndef __NET_NETNS_HASH_H
#define __NET_NETNS_HASH_H

#include <net/net_namespace.h>

static inline u32 net_hash_mix(const struct net *net)
{
	return net->bhash.hash_mix;
}

#endif /* __NET_NETNS_HASH_H */