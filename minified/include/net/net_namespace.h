#ifndef __NET_NET_NAMESPACE_H
#define __NET_NET_NAMESPACE_H

#include <linux/ns_common.h>

typedef struct net *possible_net_t;

struct net {
	atomic_t		passive;
	atomic_t		count;
	spinlock_t		rules_mod_lock;

	atomic64_t		cookie_gen;

	struct list_head	list;

	struct list_head	exit_list;
	struct llist_node	cleanup_list;

	struct vfsmount		*nsfs_ns;
	struct proc_ns		*proc_net_ns;

	struct ctl_table_set	sysctls;

	struct sock		*genl_sock;

	struct uevent_sock	*uevent_sock;
	struct list_head 	uevent_sock_list;

	struct {
		u32			hash_mix;
		struct netns_bhash	*bhash;
		struct mutex		*hash_mutex;
	} bhash;

	struct net		*peer;

	struct list_head	dev_base_head;

	struct user_namespace	*user_ns;

#ifdef CONFIG_BPF
	struct netns_bpf	bpf;
#endif
};

#ifdef CONFIG_NET_NS
extern struct net *copy_net_ns(unsigned long flags,
			struct user_namespace *user_ns, struct net *old_net);
#endif

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

static inline void write_pnet(possible_net_t *pnet, struct net *net)
{
	*pnet = net;
}

static inline bool net_eq(const struct net *net1, const struct net *net2)
{
	return net1 == net2;
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