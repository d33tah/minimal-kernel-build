#include <linux/ptrace.h>
#include <asm/kdebug.h>

unsigned long oops_begin(void)
{
	unsigned long flags;

	/* racy, but better than risking deadlock. */
	raw_local_irq_save(flags);
	return flags;
}

void __noreturn rewind_stack_and_make_dead(int signr);

void oops_end(unsigned long flags, struct pt_regs *regs, int signr)
{
	raw_local_irq_restore(flags);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	/*
	 * We're not going to return, but we might be on an IST stack or
	 * have very little stack space left.  Rewind the stack and kill
	 * the task.
	 */
	rewind_stack_and_make_dead(signr);
}

/*
 * This is gone through when something in the kernel has done something bad
 * and is about to be terminated:
 */
void die(const char *str, struct pt_regs *regs, long err)
{
	unsigned long flags = oops_begin();
	int sig = SIGSEGV;

	oops_end(flags, regs, sig);
}

void die_addr(const char *str, struct pt_regs *regs, long err, long gp_addr)
{
	unsigned long flags = oops_begin();
	int sig = SIGSEGV;

	oops_end(flags, regs, sig);
}
