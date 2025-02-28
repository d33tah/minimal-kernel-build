/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Operations on the network namespace
 */
#ifndef __NET_NET_NAMESPACE_H
#define __NET_NET_NAMESPACE_H

#include <linux/atomic.h>
#include <linux/refcount.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/sysctl.h>
#include <linux/uidgid.h>

#include <net/flow.h>
#include <net/netns/core.h>
#include <net/netns/mib.h>
#include <net/netns/unix.h>
#include <net/netns/packet.h>
#include <net/netns/ipv4.h>
#include <net/netns/ipv6.h>
#include <net/netns/nexthop.h>


#include <net/netns/netfilter.h>
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
#include <net/netns/conntrack.h>
#endif






#include <net/netns/bpf.h>

#include <net/net_trackers.h>
#include <linux/ns_common.h>
#include <linux/idr.h>
#include <linux/skbuff.h>
#include <linux/notifier.h>

struct user_namespace;
struct proc_dir_entry;
struct net_device;
struct sock;
struct ctl_table_header;
struct net_generic;
struct uevent_sock;
struct netns_ipvs;
struct bpf_prog;


#define NETDEV_HASHBITS    8
#define NETDEV_HASHENTRIES (1 << NETDEV_HASHBITS)

struct net {
	/* First cache line can be often dirtied.
	 * Do not place here read-mostly fields.
	 */
	refcount_t		passive;	/* To decide when the network
						 * namespace should be freed.
						 */
	spinlock_t		rules_mod_lock;

	atomic_t		dev_unreg_count;

	unsigned int		dev_base_seq;	/* protected by rtnl_mutex */
	int			ifindex;

	spinlock_t		nsid_lock;
	atomic_t		fnhe_genid;

	struct list_head	list;		/* list of network namespaces */
	struct list_head	exit_list;	/* To linked to call pernet exit
						 * methods on dead net (
						 * pernet_ops_rwsem read locked),
						 * or to unregister pernet ops
						 * (pernet_ops_rwsem write locked).
						 */
	struct llist_node	cleanup_list;	/* namespaces on death row */

	struct user_namespace   *user_ns;	/* Owning user namespace */
	struct ucounts		*ucounts;
	struct idr		netns_ids;

	struct ns_common	ns;
	struct ref_tracker_dir  refcnt_tracker;

	struct list_head 	dev_base_head;
	struct proc_dir_entry 	*proc_net;
	struct proc_dir_entry 	*proc_net_stat;


	struct sock 		*rtnl;			/* rtnetlink socket */
	struct sock		*genl_sock;

	struct uevent_sock	*uevent_sock;		/* uevent socket */

	struct hlist_head 	*dev_name_head;
	struct hlist_head	*dev_index_head;
	struct raw_notifier_head	netdev_chain;

	/* Note that @hash_mix can be read millions times per second,
	 * it is critical that it is on a read_mostly cache line.
	 */
	u32			hash_mix;

	struct net_device       *loopback_dev;          /* The loopback */

	/* core fib_rules */
	struct list_head	rules_ops;

	struct netns_core	core;
	struct netns_mib	mib;
	struct netns_packet	packet;
	struct netns_unix	unx;
	struct netns_nexthop	nexthop;
	struct netns_ipv4	ipv4;
#if IS_ENABLED(CONFIG_IPV6)
	struct netns_ipv6	ipv6;
#endif
#if IS_ENABLED(CONFIG_IEEE802154_6LOWPAN)
	struct netns_ieee802154_lowpan	ieee802154_lowpan;
#endif
#if defined(CONFIG_IP_SCTP) || defined(CONFIG_IP_SCTP_MODULE)
	struct netns_sctp	sctp;
#endif
	struct net_generic __rcu	*gen;

	/* Used to store attached BPF programs */
	struct netns_bpf	bpf;

	/* Note : following structs are cache line aligned */

	u64			net_cookie; /* written once */

#if IS_ENABLED(CONFIG_IP_VS)
	struct netns_ipvs	*ipvs;
#endif
#if IS_ENABLED(CONFIG_MPLS)
	struct netns_mpls	mpls;
#endif
#if IS_ENABLED(CONFIG_CAN)
	struct netns_can	can;
#endif
#if IS_ENABLED(CONFIG_MCTP)
	struct netns_mctp	mctp;
#endif
#if IS_ENABLED(CONFIG_CRYPTO_USER)
	struct sock		*crypto_nlsk;
#endif
	struct sock		*diag_nlsk;
#if IS_ENABLED(CONFIG_SMC)
	struct netns_smc	smc;
#endif
} __randomize_layout;

#include <linux/seq_file_net.h>

/* Init's network namespace */
extern struct net init_net;

#include <linux/sched.h>
#include <linux/nsproxy.h>
static inline struct net *copy_net_ns(unsigned long flags,
	struct user_namespace *user_ns, struct net *old_net)
{
	if (flags & CLONE_NEWNET)
		return ERR_PTR(-EINVAL);
	return old_net;
}

static inline void net_ns_get_ownership(const struct net *net,
					kuid_t *uid, kgid_t *gid)
{
	*uid = GLOBAL_ROOT_UID;
	*gid = GLOBAL_ROOT_GID;
}

static inline void net_ns_barrier(void) {}

static inline struct ns_common *get_net_ns(struct ns_common *ns)
{
	return ERR_PTR(-EINVAL);
}

static inline struct net *get_net_ns_by_fd(int fd)
{
	return ERR_PTR(-EINVAL);
}


extern struct list_head net_namespace_list;

struct net *get_net_ns_by_pid(pid_t pid);

#define ipx_register_sysctl()
#define ipx_unregister_sysctl()


static inline struct net *get_net(struct net *net)
{
	return net;
}

static inline void put_net(struct net *net)
{
}

static inline struct net *maybe_get_net(struct net *net)
{
	return net;
}

static inline
int net_eq(const struct net *net1, const struct net *net2)
{
	return 1;
}

static inline int check_net(const struct net *net)
{
	return 1;
}

#define net_drop_ns NULL


static inline void netns_tracker_alloc(struct net *net,
				       netns_tracker *tracker, gfp_t gfp)
{
}

static inline void netns_tracker_free(struct net *net,
				      netns_tracker *tracker)
{
}

static inline struct net *get_net_track(struct net *net,
					netns_tracker *tracker, gfp_t gfp)
{
	get_net(net);
	netns_tracker_alloc(net, tracker, gfp);
	return net;
}

static inline void put_net_track(struct net *net, netns_tracker *tracker)
{
	netns_tracker_free(net, tracker);
	put_net(net);
}

typedef struct {
} possible_net_t;

static inline void write_pnet(possible_net_t *pnet, struct net *net)
{
}

static inline struct net *read_pnet(const possible_net_t *pnet)
{
	return &init_net;
}

/* Protected by net_rwsem */
#define for_each_net(VAR)				\
	list_for_each_entry(VAR, &net_namespace_list, list)
#define for_each_net_continue_reverse(VAR)		\
	list_for_each_entry_continue_reverse(VAR, &net_namespace_list, list)
#define for_each_net_rcu(VAR)				\
	list_for_each_entry_rcu(VAR, &net_namespace_list, list)

#define __net_init	__init
#define __net_exit	__ref
#define __net_initdata	__initdata
#define __net_initconst	__initconst

int peernet2id_alloc(struct net *net, struct net *peer, gfp_t gfp);
int peernet2id(const struct net *net, struct net *peer);
bool peernet_has_id(const struct net *net, struct net *peer);
struct net *get_net_ns_by_id(const struct net *net, int id);

struct pernet_operations {
	struct list_head list;
	/*
	 * Below methods are called without any exclusive locks.
	 * More than one net may be constructed and destructed
	 * in parallel on several cpus. Every pernet_operations
	 * have to keep in mind all other pernet_operations and
	 * to introduce a locking, if they share common resources.
	 *
	 * The only time they are called with exclusive lock is
	 * from register_pernet_subsys(), unregister_pernet_subsys()
	 * register_pernet_device() and unregister_pernet_device().
	 *
	 * Exit methods using blocking RCU primitives, such as
	 * synchronize_rcu(), should be implemented via exit_batch.
	 * Then, destruction of a group of net requires single
	 * synchronize_rcu() related to these pernet_operations,
	 * instead of separate synchronize_rcu() for every net.
	 * Please, avoid synchronize_rcu() at all, where it's possible.
	 *
	 * Note that a combination of pre_exit() and exit() can
	 * be used, since a synchronize_rcu() is guaranteed between
	 * the calls.
	 */
	int (*init)(struct net *net);
	void (*pre_exit)(struct net *net);
	void (*exit)(struct net *net);
	void (*exit_batch)(struct list_head *net_exit_list);
	unsigned int *id;
	size_t size;
};

/*
 * Use these carefully.  If you implement a network device and it
 * needs per network namespace operations use device pernet operations,
 * otherwise use pernet subsys operations.
 *
 * Network interfaces need to be removed from a dying netns _before_
 * subsys notifiers can be called, as most of the network code cleanup
 * (which is done from subsys notifiers) runs with the assumption that
 * dev_remove_pack has been called so no new packets will arrive during
 * and after the cleanup functions have been called.  dev_remove_pack
 * is not per namespace so instead the guarantee of no more packets
 * arriving in a network namespace is provided by ensuring that all
 * network devices and all sockets have left the network namespace
 * before the cleanup methods are called.
 *
 * For the longest time the ipv4 icmp code was registered as a pernet
 * device which caused kernel oops, and panics during network
 * namespace cleanup.   So please don't get this wrong.
 */
int register_pernet_subsys(struct pernet_operations *);
void unregister_pernet_subsys(struct pernet_operations *);
int register_pernet_device(struct pernet_operations *);
void unregister_pernet_device(struct pernet_operations *);

struct ctl_table;

static inline int net_sysctl_init(void) { return 0; }
static inline struct ctl_table_header *register_net_sysctl(struct net *net,
	const char *path, struct ctl_table *table)
{
	return NULL;
}
static inline void unregister_net_sysctl_table(struct ctl_table_header *header)
{
}

static inline int rt_genid_ipv4(const struct net *net)
{
	return atomic_read(&net->ipv4.rt_genid);
}

#if IS_ENABLED(CONFIG_IPV6)
static inline int rt_genid_ipv6(const struct net *net)
{
	return atomic_read(&net->ipv6.fib6_sernum);
}
#endif

static inline void rt_genid_bump_ipv4(struct net *net)
{
	atomic_inc(&net->ipv4.rt_genid);
}

extern void (*__fib6_flush_trees)(struct net *net);
static inline void rt_genid_bump_ipv6(struct net *net)
{
	if (__fib6_flush_trees)
		__fib6_flush_trees(net);
}

#if IS_ENABLED(CONFIG_IEEE802154_6LOWPAN)
static inline struct netns_ieee802154_lowpan *
net_ieee802154_lowpan(struct net *net)
{
	return &net->ieee802154_lowpan;
}
#endif

/* For callers who don't really care about whether it's IPv4 or IPv6 */
static inline void rt_genid_bump_all(struct net *net)
{
	rt_genid_bump_ipv4(net);
	rt_genid_bump_ipv6(net);
}

static inline int fnhe_genid(const struct net *net)
{
	return atomic_read(&net->fnhe_genid);
}

static inline void fnhe_genid_bump(struct net *net)
{
	atomic_inc(&net->fnhe_genid);
}

static inline void net_ns_init(void) {}

#endif /* __NET_NET_NAMESPACE_H */
