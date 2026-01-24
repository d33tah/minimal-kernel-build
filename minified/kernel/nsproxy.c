
#include <linux/slab.h>
#include <linux/nsproxy.h>
#include <linux/init_task.h>
#include <linux/mnt_namespace.h>
#include <linux/utsname.h>
#include <linux/pid_namespace.h>
#include <net/net_namespace.h>
#include <linux/time_namespace.h>

/* struct ipc_namespace removed - never used */
#include <linux/fs_struct.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/file.h>
#include <linux/syscalls.h>
#include <linux/cgroup.h>
#include <linux/perf_event.h>

static struct kmem_cache *nsproxy_cachep;

struct nsproxy init_nsproxy = {
	.count = ATOMIC_INIT(1),
	.uts_ns = &init_uts_ns,
	.mnt_ns = NULL,
	.pid_ns_for_children = &init_pid_ns,
};

/* create_new_namespaces removed - no CLONE_NEW* flags ever used */

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

/* switch_task_namespaces inlined into exit_task_namespaces */

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
