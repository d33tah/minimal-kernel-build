#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/ptrace.h>
#include <linux/sched/debug.h>

static int die_counter;

static struct pt_regs exec_summary_regs;

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

unsigned long oops_begin(void)
{
	return 0;
}
NOKPROBE_SYMBOL(oops_begin);

/* Anchor-stub: the die/oops crash path is runtime-dead in a healthy
 * boot+print+stay-alive kernel (oops_begin already stubbed, tick #320).
 * Kept link-live for its crash-path callers (die/die_addr/page_fault_oops). */
void oops_end(unsigned long flags, struct pt_regs *regs, int signr)
{
}
NOKPROBE_SYMBOL(oops_end);

static void __die_header(const char *str, struct pt_regs *regs, long err)
{
	/* Stub: minimal header for crash report */
	if (!die_counter)
		exec_summary_regs = *regs;
	printk(KERN_DEFAULT "%s [#%d]\n", str, ++die_counter);
}
NOKPROBE_SYMBOL(__die_header);

static int __die_body(const char *str, struct pt_regs *regs, long err)
{
	/* Stub: minimal crash info */
	return notify_die(DIE_OOPS, str, regs, err,
			current->thread.trap_nr, SIGSEGV) == NOTIFY_STOP;
}
NOKPROBE_SYMBOL(__die_body);

int __die(const char *str, struct pt_regs *regs, long err)
{
	__die_header(str, regs, err);
	return __die_body(str, regs, err);
}
NOKPROBE_SYMBOL(__die);

void die(const char *str, struct pt_regs *regs, long err)
{
	unsigned long flags = oops_begin();
	int sig = SIGSEGV;

	if (__die(str, regs, err))
		sig = 0;
	oops_end(flags, regs, sig);
}

void die_addr(const char *str, struct pt_regs *regs, long err, long gp_addr)
{
	unsigned long flags = oops_begin();
	int sig = SIGSEGV;

	__die_header(str, regs, err);
	if (__die_body(str, regs, err))
		sig = 0;
	oops_end(flags, regs, sig);
}

void show_regs(struct pt_regs *regs)
{
	/* Stub: minimal register display for crash */
	__show_regs(regs, SHOW_REGS_ALL, KERN_DEFAULT);
}
