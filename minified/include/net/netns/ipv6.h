/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ipv6 in net namespaces
 */



#ifndef __NETNS_IPV6_H__
#define __NETNS_IPV6_H__
#include <net/dst_ops.h>
#include <uapi/linux/icmpv6.h>

struct ctl_table_header;

struct netns_sysctl_ipv6 {
	int flush_delay;
	int ip6_rt_max_size;
	int ip6_rt_gc_min_interval;
	int ip6_rt_gc_timeout;
	int ip6_rt_gc_interval;
	int ip6_rt_gc_elasticity;
	int ip6_rt_mtu_expires;
	int ip6_rt_min_advmss;
	u32 multipath_hash_fields;
	u8 multipath_hash_policy;
	u8 bindv6only;
	u8 flowlabel_consistency;
	u8 auto_flowlabels;
	int icmpv6_time;
	u8 icmpv6_echo_ignore_all;
	u8 icmpv6_echo_ignore_multicast;
	u8 icmpv6_echo_ignore_anycast;
	DECLARE_BITMAP(icmpv6_ratemask, ICMPV6_MSG_MAX + 1);
	unsigned long *icmpv6_ratemask_ptr;
	u8 anycast_src_echo_reply;
	u8 ip_nonlocal_bind;
	u8 fwmark_reflect;
	u8 flowlabel_state_ranges;
	int idgen_retries;
	int idgen_delay;
	int flowlabel_reflect;
	int max_dst_opts_cnt;
	int max_hbh_opts_cnt;
	int max_dst_opts_len;
	int max_hbh_opts_len;
	int seg6_flowlabel;
	u32 ioam6_id;
	u64 ioam6_id_wide;
	bool skip_notify_on_dev_down;
	u8 fib_notify_on_flag_change;
};

struct netns_ipv6 {
	/* Keep ip6_dst_ops at the beginning of netns_sysctl_ipv6 */
	struct dst_ops		ip6_dst_ops;

	struct netns_sysctl_ipv6 sysctl;
	struct ipv6_devconf	*devconf_all;
	struct ipv6_devconf	*devconf_dflt;
	struct inet_peer_base	*peers;
	struct fqdir		*fqdir;
	struct fib6_info	*fib6_null_entry;
	struct rt6_info		*ip6_null_entry;
	struct rt6_statistics   *rt6_stats;
	struct timer_list       ip6_fib_timer;
	struct hlist_head       *fib_table_hash;
	struct fib6_table       *fib6_main_tbl;
	struct list_head	fib6_walkers;
	rwlock_t		fib6_walker_lock;
	spinlock_t		fib6_gc_lock;
	atomic_t		ip6_rt_gc_expire;
	unsigned long		ip6_rt_last_gc;
	unsigned char		flowlabel_has_excl;
	struct sock             *ndisc_sk;
	struct sock             *tcp_sk;
	struct sock             *igmp_sk;
	struct sock		*mc_autojoin_sk;

	struct hlist_head	*inet6_addr_lst;
	spinlock_t		addrconf_hash_lock;
	struct delayed_work	addr_chk_work;

	atomic_t		dev_addr_genid;
	atomic_t		fib6_sernum;
	struct seg6_pernet_data *seg6_data;
	struct fib_notifier_ops	*notifier_ops;
	struct fib_notifier_ops	*ip6mr_notifier_ops;
	unsigned int ipmr_seq; /* protected by rtnl_mutex */
	struct {
		struct hlist_head head;
		spinlock_t	lock;
		u32		seq;
	} ip6addrlbl_table;
	struct ioam6_pernet_data *ioam6_data;
};

#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV6)
struct netns_nf_frag {
	struct fqdir	*fqdir;
};
#endif

#endif
