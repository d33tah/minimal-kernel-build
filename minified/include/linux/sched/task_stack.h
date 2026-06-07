#ifndef _LINUX_SCHED_TASK_STACK_H
#define _LINUX_SCHED_TASK_STACK_H

#include <linux/sched.h>

static __always_inline void *task_stack_page(const struct task_struct *task)
{
	return task->stack;
}

static inline unsigned long *end_of_stack(const struct task_struct *task)
{
	return task->stack;
}

extern void set_task_stack_end_magic(struct task_struct *tsk);

#endif  
