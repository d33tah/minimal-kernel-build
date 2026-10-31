
#include <linux/utsname.h>

struct nsproxy init_nsproxy = {
	.count = ATOMIC_INIT(1),
	.uts_ns = &init_uts_ns,
	.pid_ns_for_children = &init_pid_ns,
};

int copy_namespaces(unsigned long flags, struct task_struct *tsk)
{
	struct nsproxy *old_ns = tsk->nsproxy;

	/*
	 * This minimal kernel never clones namespaces: no task ever passes a
	 * CLONE_NEW* flag and no time namespace is ever created (CONFIG_TIME_NS
	 * off). The shared nsproxy is simply pinned and inherited.
	 */
	get_nsproxy(old_ns);
	return 0;
}
