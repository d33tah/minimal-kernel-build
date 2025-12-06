#ifndef _LINUX_SCHED_TASK_STACK_H
#define _LINUX_SCHED_TASK_STACK_H


#include <linux/sched.h>
#include <linux/magic.h>


static __always_inline void *task_stack_page(const struct task_struct *task)
{
	return task->stack;
}

#define setup_thread_stack(new,old)	do { } while(0)

static inline unsigned long *end_of_stack(const struct task_struct *task)
{
	return task->stack;
}


static inline void *try_get_task_stack(struct task_struct *tsk)
{
	return refcount_inc_not_zero(&tsk->stack_refcount) ?
		task_stack_page(tsk) : NULL;
}

extern void put_task_stack(struct task_struct *tsk);

void exit_task_stack_account(struct task_struct *tsk);

#define task_stack_end_corrupted(task) \
		(*(end_of_stack(task)) != STACK_END_MAGIC)

/* object_is_on_stack removed - unused */

extern void thread_stack_cache_init(void);

extern void set_task_stack_end_magic(struct task_struct *tsk);

/* kstack_end removed - unused */

#endif  
