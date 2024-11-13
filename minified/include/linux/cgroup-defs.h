/* SPDX-License-Identifier: GPL-2.0 */
/*
 * linux/cgroup-defs.h - basic definitions for cgroup
 *
 * This file provides basic type and interface.  Include this file directly
 * only if necessary to avoid cyclic dependencies.
 */
#ifndef _LINUX_CGROUP_DEFS_H
#define _LINUX_CGROUP_DEFS_H

#include <linux/limits.h>
#include <linux/list.h>
#include <linux/idr.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/percpu-refcount.h>
#include <linux/percpu-rwsem.h>
#include <linux/u64_stats_sync.h>
#include <linux/workqueue.h>
#include <linux/bpf-cgroup-defs.h>
#include <linux/psi_types.h>


#define CGROUP_SUBSYS_COUNT 0

static inline void cgroup_threadgroup_change_begin(struct task_struct *tsk)
{
	might_sleep();
}

static inline void cgroup_threadgroup_change_end(struct task_struct *tsk) {}


#ifdef CONFIG_SOCK_CGROUP_DATA

/*
 * sock_cgroup_data is embedded at sock->sk_cgrp_data and contains
 * per-socket cgroup information except for memcg association.
 *
 * On legacy hierarchies, net_prio and net_cls controllers directly
 * set attributes on each sock which can then be tested by the network
 * layer. On the default hierarchy, each sock is associated with the
 * cgroup it was created in and the networking layer can match the
 * cgroup directly.
 */
struct sock_cgroup_data {
	struct cgroup	*cgroup; /* v2 */
#ifdef CONFIG_CGROUP_NET_CLASSID
	u32		classid; /* v1 */
#endif
#ifdef CONFIG_CGROUP_NET_PRIO
	u16		prioidx; /* v1 */
#endif
};

static inline u16 sock_cgroup_prioidx(const struct sock_cgroup_data *skcd)
{
#ifdef CONFIG_CGROUP_NET_PRIO
	return READ_ONCE(skcd->prioidx);
#else
	return 1;
#endif
}

static inline u32 sock_cgroup_classid(const struct sock_cgroup_data *skcd)
{
#ifdef CONFIG_CGROUP_NET_CLASSID
	return READ_ONCE(skcd->classid);
#else
	return 0;
#endif
}

static inline void sock_cgroup_set_prioidx(struct sock_cgroup_data *skcd,
					   u16 prioidx)
{
#ifdef CONFIG_CGROUP_NET_PRIO
	WRITE_ONCE(skcd->prioidx, prioidx);
#endif
}

static inline void sock_cgroup_set_classid(struct sock_cgroup_data *skcd,
					   u32 classid)
{
#ifdef CONFIG_CGROUP_NET_CLASSID
	WRITE_ONCE(skcd->classid, classid);
#endif
}

#else	/* CONFIG_SOCK_CGROUP_DATA */

struct sock_cgroup_data {
};

#endif	/* CONFIG_SOCK_CGROUP_DATA */

#endif	/* _LINUX_CGROUP_DEFS_H */
