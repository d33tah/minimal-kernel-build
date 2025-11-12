/*
 * Stubbed stack trace management
 */
#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/export.h>

/* Stub: arch_stack_walk */
void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	/* No-op: no stack walking in minimal kernel */
}

/* Stub: arch_stack_walk_reliable */
int arch_stack_walk_reliable(stack_trace_consume_fn consume_entry,
			     void *cookie, struct task_struct *task)
{
	return -EINVAL;
}

/* Stub: arch_stack_walk_user */
void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
			  const struct pt_regs *regs)
{
	/* No-op: no userspace stack walking */
}
