#ifndef __LINUX_STACKTRACE_H
#define __LINUX_STACKTRACE_H

#include <linux/types.h>
#include <asm/errno.h>

struct task_struct;
struct pt_regs;


typedef bool (*stack_trace_consume_fn)(void *cookie, unsigned long addr);
void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs);

int arch_stack_walk_reliable(stack_trace_consume_fn consume_entry, void *cookie,
			     struct task_struct *task);

void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
			  const struct pt_regs *regs);

/* stack_trace_save_tsk_reliable removed - unused */

#endif  
