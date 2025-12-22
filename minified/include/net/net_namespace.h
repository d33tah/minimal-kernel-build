/* Minimal net_namespace.h - networking disabled */
#ifndef __NET_NET_NAMESPACE_H
#define __NET_NET_NAMESPACE_H

#include <linux/ns_common.h>

typedef struct net *possible_net_t;

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

static inline struct net *read_pnet(possible_net_t *pnet)
{
	return *pnet;
}


static inline struct net *copy_net_ns(unsigned long flags,
	struct user_namespace *user_ns, struct net *old_ns)
{
	if (flags & CLONE_NEWNET)
		return ERR_PTR(-EINVAL);

	return old_ns;
}

extern struct net init_net;

#endif  