
#include <linux/slab.h>
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
/* init_ipc_ns extern removed - never used */
static inline struct ipc_namespace *copy_ipcs(unsigned long flags,
					      struct user_namespace *user_ns,
					      struct ipc_namespace *ns)
{
	if (flags & CLONE_NEWIPC)
		return ERR_PTR(-EINVAL);
	return ns;
}
/* put_ipc_ns removed - empty stub */
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

static struct nsproxy *create_new_namespaces(unsigned long flags,
					     struct task_struct *tsk,
					     struct user_namespace *user_ns,
					     struct fs_struct *new_fs)
{
	struct nsproxy *new_nsp;
	int err;

	new_nsp = kmem_cache_alloc(nsproxy_cachep, GFP_KERNEL);
	if (!new_nsp)
		return ERR_PTR(-ENOMEM);
	atomic_set(&new_nsp->count, 1);

	new_nsp->mnt_ns =
		copy_mnt_ns(flags, tsk->nsproxy->mnt_ns, user_ns, new_fs);
	if (IS_ERR(new_nsp->mnt_ns)) {
		err = PTR_ERR(new_nsp->mnt_ns);
		goto out_ns;
	}

	new_nsp->uts_ns = copy_utsname(flags, user_ns, tsk->nsproxy->uts_ns);
	if (IS_ERR(new_nsp->uts_ns)) {
		err = PTR_ERR(new_nsp->uts_ns);
		goto out_uts;
	}

	new_nsp->ipc_ns = copy_ipcs(flags, user_ns, tsk->nsproxy->ipc_ns);
	if (IS_ERR(new_nsp->ipc_ns)) {
		err = PTR_ERR(new_nsp->ipc_ns);
		goto out_ipc;
	}

	new_nsp->pid_ns_for_children =
		copy_pid_ns(flags, user_ns, tsk->nsproxy->pid_ns_for_children);
	if (IS_ERR(new_nsp->pid_ns_for_children)) {
		err = PTR_ERR(new_nsp->pid_ns_for_children);
		goto out_pid;
	}

	new_nsp->cgroup_ns =
		copy_cgroup_ns(flags, user_ns, tsk->nsproxy->cgroup_ns);
	if (IS_ERR(new_nsp->cgroup_ns)) {
		err = PTR_ERR(new_nsp->cgroup_ns);
		goto out_cgroup;
	}

	new_nsp->net_ns = copy_net_ns(flags, user_ns, tsk->nsproxy->net_ns);
	if (IS_ERR(new_nsp->net_ns)) {
		err = PTR_ERR(new_nsp->net_ns);
		goto out_net;
	}

	new_nsp->time_ns_for_children = copy_time_ns(
		flags, user_ns, tsk->nsproxy->time_ns_for_children);
	if (IS_ERR(new_nsp->time_ns_for_children)) {
		err = PTR_ERR(new_nsp->time_ns_for_children);
		goto out_time;
	}
	new_nsp->time_ns = get_time_ns(tsk->nsproxy->time_ns);

	return new_nsp;

out_time:
	put_net(new_nsp->net_ns);
out_net:
	put_cgroup_ns(new_nsp->cgroup_ns);
out_cgroup:
	/* put_pid_ns removed - empty stub */
out_pid:
	/* put_ipc_ns call removed - empty stub */
out_ipc:
	/* put_uts_ns removed - empty stub */
out_uts:
	if (new_nsp->mnt_ns)
		put_mnt_ns(new_nsp->mnt_ns);
out_ns:
	kmem_cache_free(nsproxy_cachep, new_nsp);
	return ERR_PTR(err);
}

int copy_namespaces(unsigned long flags, struct task_struct *tsk)
{
	struct nsproxy *old_ns = tsk->nsproxy;
	struct user_namespace *user_ns = task_cred_xxx(tsk, user_ns);
	struct nsproxy *new_ns;

	if (likely(!(flags &
		     (CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID |
		      CLONE_NEWNET | CLONE_NEWCGROUP | CLONE_NEWTIME)))) {
		if (likely(old_ns->time_ns_for_children == old_ns->time_ns)) {
			get_nsproxy(old_ns);
			return 0;
		}
	}
	/* ns_capable always returns true - removed dead capability check */

	if ((flags & (CLONE_NEWIPC | CLONE_SYSVSEM)) ==
	    (CLONE_NEWIPC | CLONE_SYSVSEM))
		return -EINVAL;

	new_ns = create_new_namespaces(flags, tsk, user_ns, tsk->fs);
	if (IS_ERR(new_ns))
		return PTR_ERR(new_ns);

	timens_on_fork(new_ns, tsk);

	tsk->nsproxy = new_ns;
	return 0;
}

void free_nsproxy(struct nsproxy *ns)
{
	if (ns->mnt_ns)
		put_mnt_ns(ns->mnt_ns);
	/* put_uts_ns, put_ipc_ns, put_pid_ns removed - empty stubs */
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

SYSCALL_DEFINE2(setns, int, fd, int, flags)
{
	/* Stubbed: setns not needed for minimal kernel */
	return -ENOSYS;
}

int __init nsproxy_cache_init(void)
{
	nsproxy_cachep = KMEM_CACHE(nsproxy, SLAB_PANIC | SLAB_ACCOUNT);
	return 0;
}
