#include <asm/kdebug.h>
#include <linux/ptrace.h>

void __noreturn rewind_stack_and_make_dead(int signr);

unsigned long oops_begin(void)
{
	unsigned long flags;

	raw_local_irq_save(flags);
	console_verbose();
	bust_spinlocks(1);
	return flags;
}

void oops_end(unsigned long flags, struct pt_regs *regs, int signr)
{
	bust_spinlocks(0);
	raw_local_irq_restore(flags);

	if (!signr)
		return;
	if (in_interrupt())
		panic("Fatal exception in interrupt");

	rewind_stack_and_make_dead(signr);
}

int __die(const char *str, struct pt_regs *regs, long err)
{
	return 0;
}

void die(const char *str, struct pt_regs *regs, long err)
{
	unsigned long flags = oops_begin();
	__die(str, regs, err);
	oops_end(flags, regs, SIGSEGV);
}
