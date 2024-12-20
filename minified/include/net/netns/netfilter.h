/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_NETFILTER_H
#define __NETNS_NETFILTER_H

#include <linux/netfilter_defs.h>

struct proc_dir_entry;
struct nf_logger;
struct nf_queue_handler;

struct netns_nf {
	const struct nf_logger __rcu *nf_loggers[NFPROTO_NUMPROTO];
	struct nf_hook_entries __rcu *hooks_ipv4[NF_INET_NUMHOOKS];
	struct nf_hook_entries __rcu *hooks_ipv6[NF_INET_NUMHOOKS];
#if IS_ENABLED(CONFIG_DECNET)
	struct nf_hook_entries __rcu *hooks_decnet[NF_DN_NUMHOOKS];
#endif
#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV4)
	unsigned int defrag_ipv4_users;
#endif
#if IS_ENABLED(CONFIG_NF_DEFRAG_IPV6)
	unsigned int defrag_ipv6_users;
#endif
};
#endif
