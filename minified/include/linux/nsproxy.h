#ifndef _LINUX_NSPROXY_H
#define _LINUX_NSPROXY_H

#include <linux/sched.h>

struct uts_namespace;
struct pid_namespace;

struct nsproxy {
	atomic_t count;
	struct uts_namespace *uts_ns;
	struct pid_namespace *pid_ns_for_children;
};
extern struct nsproxy init_nsproxy;

int copy_namespaces(unsigned long flags, struct task_struct *tsk);

static inline void get_nsproxy(struct nsproxy *ns)
{
	atomic_inc(&ns->count);
}

#endif
