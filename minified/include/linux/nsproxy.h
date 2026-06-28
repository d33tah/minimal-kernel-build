#ifndef _LINUX_NSPROXY_H
#define _LINUX_NSPROXY_H

#include <linux/spinlock.h>
#include <linux/sched.h>

struct mnt_namespace;
struct uts_namespace;
struct pid_namespace;
struct fs_struct;

struct nsproxy {
	atomic_t count;
	struct uts_namespace *uts_ns;
	struct mnt_namespace *mnt_ns;
	struct pid_namespace *pid_ns_for_children;
};
extern struct nsproxy init_nsproxy;

int copy_namespaces(unsigned long flags, struct task_struct *tsk);
void exit_task_namespaces(struct task_struct *tsk);
/* switch_task_namespaces / put_nsproxy / free_nsproxy: cascade-deleted with
 * exit_task_namespaces (nsproxy teardown is runtime-dead on a 1-shot boot). */
int __init nsproxy_cache_init(void);

static inline void get_nsproxy(struct nsproxy *ns)
{
	atomic_inc(&ns->count);
}

#endif
