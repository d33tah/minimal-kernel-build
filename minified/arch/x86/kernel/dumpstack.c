#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/sched/debug.h>

static void show_trace_log_lvl(struct task_struct *task, struct pt_regs *regs,
			unsigned long *stack, const char *log_lvl)
{
	/* Stub: stack trace printing not needed for minimal kernel */
	printk("%sCall Trace: <stubbed>\n", log_lvl);
}


void show_stack_regs(struct pt_regs *regs)
{
	show_trace_log_lvl(current, regs, NULL, KERN_DEFAULT);
}

/* Anchor-stub: die/die_addr are the runtime-dead crash path in a healthy
 * boot+print+stay-alive kernel. Kept link-live for their crash-path callers
 * in arch/x86/kernel/traps.c. The die-report internals (__die/__die_header/
 * __die_body) and oops_begin/oops_end were cascade-deleted (tick #323) once
 * the fault-path anchor page_fault_oops was stubbed too. */
void die(const char *str, struct pt_regs *regs, long err)
{
}

void die_addr(const char *str, struct pt_regs *regs, long err, long gp_addr)
{
}

void show_regs(struct pt_regs *regs)
{
	/* Stub: minimal register display for crash */
	__show_regs(regs, SHOW_REGS_ALL, KERN_DEFAULT);
}
