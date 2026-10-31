#ifndef _LINUX_PID_NS_H
#define _LINUX_PID_NS_H

#include <linux/mm.h>

struct pid_namespace {
	int last_pid;
	struct kmem_cache *pid_cachep;
	unsigned int level;
} __randomize_layout;

extern struct pid_namespace init_pid_ns;

static inline struct pid_namespace *get_pid_ns(struct pid_namespace *ns)
{
	return ns;
}

extern struct pid_namespace *task_active_pid_ns(struct task_struct *tsk);
void pid_init(void);

#endif
