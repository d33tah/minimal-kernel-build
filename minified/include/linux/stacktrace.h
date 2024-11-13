/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_STACKTRACE_H
#define __LINUX_STACKTRACE_H

#include <linux/types.h>
#include <asm/errno.h>

struct task_struct;
struct pt_regs;


/**
 * stack_trace_consume_fn - Callback for arch_stack_walk()
 * @cookie:	Caller supplied pointer handed back by arch_stack_walk()
 * @addr:	The stack entry address to consume
 *
 * Return:	True, if the entry was consumed or skipped
 *		False, if there is no space left to store
 */
typedef bool (*stack_trace_consume_fn)(void *cookie, unsigned long addr);
/**
 * arch_stack_walk - Architecture specific function to walk the stack
 * @consume_entry:	Callback which is invoked by the architecture code for
 *			each entry.
 * @cookie:		Caller supplied pointer which is handed back to
 *			@consume_entry
 * @task:		Pointer to a task struct, can be NULL
 * @regs:		Pointer to registers, can be NULL
 *
 * ============ ======= ============================================
 * task	        regs
 * ============ ======= ============================================
 * task		NULL	Stack trace from task (can be current)
 * current	regs	Stack trace starting on regs->stackpointer
 * ============ ======= ============================================
 */
void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs);

/**
 * arch_stack_walk_reliable - Architecture specific function to walk the
 *			      stack reliably
 *
 * @consume_entry:	Callback which is invoked by the architecture code for
 *			each entry.
 * @cookie:		Caller supplied pointer which is handed back to
 *			@consume_entry
 * @task:		Pointer to a task struct, can be NULL
 *
 * This function returns an error if it detects any unreliable
 * features of the stack. Otherwise it guarantees that the stack
 * trace is reliable.
 *
 * If the task is not 'current', the caller *must* ensure the task is
 * inactive and its stack is pinned.
 */
int arch_stack_walk_reliable(stack_trace_consume_fn consume_entry, void *cookie,
			     struct task_struct *task);

void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
			  const struct pt_regs *regs);


static inline int stack_trace_save_tsk_reliable(struct task_struct *tsk,
						unsigned long *store,
						unsigned int size)
{
	return -ENOSYS;
}

#endif /* __LINUX_STACKTRACE_H */
