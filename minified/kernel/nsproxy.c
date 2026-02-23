#include <linux/utsname.h>
struct net {
	atomic_t count;
	struct user_namespace *user_ns;
};
struct time_namespace;


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

int __init nsproxy_cache_init(void)
{
	return 0;
}
