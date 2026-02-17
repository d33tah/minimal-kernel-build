
#include <linux/slab.h>
#include <linux/nsproxy.h>
#include <linux/init_task.h>
struct mnt_namespace;
extern void put_mnt_ns(struct mnt_namespace *ns);
#include <linux/utsname.h>
#include <linux/pid_namespace.h>
struct net {
	atomic_t count;
	struct user_namespace *user_ns;
};
static inline void put_net(struct net *net)
{
}
#include <linux/ns_common.h>
#include <linux/err.h>

#ifndef _LINUX_TIMENS_H
#define _LINUX_TIMENS_H

struct user_namespace;
extern struct user_namespace init_user_ns;

struct timens_offsets {
	struct timespec64 monotonic;
	struct timespec64 boottime;
};

struct time_namespace {
	struct user_namespace *user_ns;
	struct ucounts *ucounts;
	struct ns_common ns;
	struct timens_offsets offsets;
	struct page *vvar_page;

	bool frozen_offsets;
} __randomize_layout;

static inline void put_time_ns(struct time_namespace *ns)
{
}

#endif /* _LINUX_TIMENS_H */

#include <linux/fs_struct.h>
#include <linux/proc_ns.h>
#include <linux/file.h>
#include <linux/cgroup.h>

static struct kmem_cache *nsproxy_cachep;

struct nsproxy init_nsproxy = {
	.count = ATOMIC_INIT(1),
	.uts_ns = &init_uts_ns,
	.mnt_ns = NULL,
	.pid_ns_for_children = &init_pid_ns,
};

int copy_namespaces(struct task_struct *tsk)
{
	struct nsproxy *old_ns = tsk->nsproxy;
	/* No CLONE_NEW* flags ever used - always share nsproxy */
	atomic_inc(&old_ns->count); /* get_nsproxy inlined */
	return 0;
}

void free_nsproxy(struct nsproxy *ns)
{
	if (ns->mnt_ns)
		put_mnt_ns(ns->mnt_ns);
	if (ns->time_ns)
		put_time_ns(ns->time_ns);
	if (ns->time_ns_for_children)
		put_time_ns(ns->time_ns_for_children);
	put_cgroup_ns(ns->cgroup_ns);
	put_net(ns->net_ns);
	kmem_cache_free(nsproxy_cachep, ns);
}

void exit_task_namespaces(struct task_struct *p)
{
	struct nsproxy *ns;

	task_lock(p);
	ns = p->nsproxy;
	p->nsproxy = NULL;
	task_unlock(p);

	if (ns)
		put_nsproxy(ns);
}

/* setns replaced with COND_SYSCALL */

int __init nsproxy_cache_init(void)
{
	nsproxy_cachep = KMEM_CACHE(nsproxy, SLAB_PANIC | SLAB_ACCOUNT);
	return 0;
}
