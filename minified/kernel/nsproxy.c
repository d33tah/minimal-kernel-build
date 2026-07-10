
#include <linux/sched/task.h>
#include <linux/utsname.h>
#include <linux/pid_namespace.h>


struct nsproxy init_nsproxy = {
	.count			= ATOMIC_INIT(1),
	.uts_ns			= &init_uts_ns,
	.mnt_ns			= NULL,
	.pid_ns_for_children	= &init_pid_ns,
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

void exit_task_namespaces(struct task_struct *p)
{
	/*
	 * RUNTIME-DEAD ANCHOR-STUB: detaches the dying task's nsproxy. Both call
	 * sites are runtime-dead on this 1-shot boot: do_exit's tail (init panics
	 * before reaching it, HIT=False) and copy_process's bad_fork_cleanup_io
	 * rollback (copy_process always succeeds at boot, HIT=False). The whole
	 * private teardown subtree (switch_task_namespaces -> put_nsproxy ->
	 * free_nsproxy) was cascade-deleted; nothing ever drops the shared,
	 * never-cloned nsproxy on this boot (copy_namespaces only get_nsproxy's it).
	 */
}

int __init nsproxy_cache_init(void)
{
	/*
	 * The nsproxy kmem_cache used to back cloned namespaces, but this kernel
	 * never clones (copy_namespaces only pins the shared init_nsproxy) and
	 * never frees one (free_nsproxy was cascade-deleted with exit_task_namespaces).
	 * No allocation ever comes from this cache, so it is no longer created.
	 */
	return 0;
}
