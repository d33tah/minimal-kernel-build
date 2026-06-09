
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/nsproxy.h>
#include <linux/init_task.h>
#include <linux/mnt_namespace.h>
#include <linux/utsname.h>
#include <linux/pid_namespace.h>
#include <net/net_namespace.h>
#include <linux/time_namespace.h>

struct ipc_namespace {
	struct user_namespace *user_ns;
	struct ns_common ns;
};
static inline void put_ipc_ns(struct ipc_namespace *ns) {}
#include <linux/fs_struct.h>
#include <linux/proc_fs.h>
#include <linux/proc_ns.h>
#include <linux/file.h>
#include <linux/syscalls.h>
#include <linux/cgroup.h>
#include <linux/perf_event.h>

static struct kmem_cache *nsproxy_cachep;

struct nsproxy init_nsproxy = {
	.count			= ATOMIC_INIT(1),
	.uts_ns			= &init_uts_ns,
	.mnt_ns			= NULL,
	.pid_ns_for_children	= &init_pid_ns,
};

int copy_namespaces(unsigned long flags, struct task_struct *tsk)
{
	struct nsproxy *old_ns = tsk->nsproxy;
	struct user_namespace *user_ns = task_cred_xxx(tsk, user_ns);

	/*
	 * This minimal kernel never clones namespaces: no task ever passes a
	 * CLONE_NEW* flag and no time namespace is ever created (CONFIG_TIME_NS
	 * off, so time_ns_for_children == time_ns always). The shared nsproxy
	 * is simply pinned and inherited.
	 */
	if (!(flags & (CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC |
		       CLONE_NEWPID | CLONE_NEWNET |
		       CLONE_NEWCGROUP | CLONE_NEWTIME))) {
		get_nsproxy(old_ns);
		return 0;
	}

	if (!ns_capable(user_ns, CAP_SYS_ADMIN))
		return -EPERM;

	return -EINVAL;
}

void free_nsproxy(struct nsproxy *ns)
{
	if (ns->mnt_ns)
		put_mnt_ns(ns->mnt_ns);
	if (ns->uts_ns)
		put_uts_ns(ns->uts_ns);
	if (ns->ipc_ns)
		put_ipc_ns(ns->ipc_ns);
	if (ns->pid_ns_for_children)
		put_pid_ns(ns->pid_ns_for_children);
	if (ns->time_ns)
		put_time_ns(ns->time_ns);
	if (ns->time_ns_for_children)
		put_time_ns(ns->time_ns_for_children);
	put_cgroup_ns(ns->cgroup_ns);
	put_net(ns->net_ns);
	kmem_cache_free(nsproxy_cachep, ns);
}


static void switch_task_namespaces(struct task_struct *p, struct nsproxy *new)
{
	struct nsproxy *ns;

	might_sleep();

	task_lock(p);
	ns = p->nsproxy;
	p->nsproxy = new;
	task_unlock(p);

	if (ns)
		put_nsproxy(ns);
}

void exit_task_namespaces(struct task_struct *p)
{
	switch_task_namespaces(p, NULL);
}

SYSCALL_DEFINE2(setns, int, fd, int, flags)
{
	/* Stubbed: setns not needed for minimal kernel */
	return -ENOSYS;
}

int __init nsproxy_cache_init(void)
{
	nsproxy_cachep = KMEM_CACHE(nsproxy, SLAB_PANIC|SLAB_ACCOUNT);
	return 0;
}
