#ifndef _LINUX_PID_H
#define _LINUX_PID_H

#include <linux/rcupdate.h>
#include <linux/refcount.h>

enum pid_type {
	PIDTYPE_PID,
};

struct upid {
	int nr;
	struct pid_namespace *ns;
};

struct pid {
	refcount_t count;
	unsigned int level;
	struct upid numbers[1];
};

extern struct pid init_struct_pid;

static inline struct pid *get_pid(struct pid *pid)
{
	if (pid)
		refcount_inc(&pid->count);
	return pid;
}

extern void put_pid(struct pid *pid);

extern struct pid *get_task_pid(struct task_struct *task, enum pid_type type);

struct pid_namespace;
extern struct pid_namespace init_pid_ns;

extern struct pid *alloc_pid(struct pid_namespace *ns);

static inline struct pid_namespace *ns_of_pid(struct pid *pid)
{
	struct pid_namespace *ns = NULL;
	if (pid)
		ns = pid->numbers[pid->level].ns;
	return ns;
}

static inline pid_t pid_nr(struct pid *pid)
{
	pid_t nr = 0;
	if (pid)
		nr = pid->numbers[0].nr;
	return nr;
}
pid_t pid_vnr(struct pid *pid);

#endif
