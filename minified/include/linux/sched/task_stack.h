#ifndef _LINUX_SCHED_TASK_STACK_H
#define _LINUX_SCHED_TASK_STACK_H


#include <linux/sched.h>
#include <linux/magic.h>


static __always_inline void *task_stack_page(const struct task_struct *task)
{
	return task->stack;
}

/* setup_thread_stack macro removed - no callers */

static inline unsigned long *end_of_stack(const struct task_struct *task)
{
	return task->stack;
}

extern void put_task_stack(struct task_struct *tsk);

void exit_task_stack_account(struct task_struct *tsk);

/* thread_stack_cache_init removed - never called */

extern void set_task_stack_end_magic(struct task_struct *tsk);


#endif  
