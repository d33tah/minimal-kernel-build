/* Minimal net_namespace.h - networking disabled */
#ifndef __NET_NET_NAMESPACE_H
#define __NET_NET_NAMESPACE_H

#include <linux/ns_common.h>

/* possible_net_t removed - unused */

/* Minimal struct net - only fields needed for namespace stubs */
struct net {
	atomic_t		count;
	struct user_namespace	*user_ns;
};

static inline struct net *get_net(struct net *net)
{
	return net;
}

static inline void put_net(struct net *net)
{
}

/* copy_net_ns removed - no callers */

#endif