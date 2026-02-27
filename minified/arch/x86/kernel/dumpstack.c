#ifndef NOKPROBE_SYMBOL
#define NOKPROBE_SYMBOL(fname) /* kprobes disabled */
#endif
#include <asm/kdebug.h>
#include <linux/ptrace.h>

static arch_spinlock_t die_lock = __ARCH_SPIN_LOCK_UNLOCKED;
static int die_owner = -1;
static unsigned int die_nest_count;

unsigned long oops_begin(void)
{
	int cpu;
	unsigned long flags;

	debug_locks_off(); /* oops_enter inlined */

	raw_local_irq_save(flags);
	cpu = smp_processor_id();
	if (!arch_spin_trylock(&die_lock)) {
		if (cpu == die_owner)
			;
		else
			arch_spin_lock(&die_lock);
	}
	die_nest_count++;
	die_owner = cpu;
	console_verbose();
	bust_spinlocks(1);
	return flags;
}
NOKPROBE_SYMBOL(oops_begin);

void __noreturn rewind_stack_and_make_dead(int signr);

void oops_end(unsigned long flags, struct pt_regs *regs, int signr)
{
	bust_spinlocks(0);
	die_owner = -1;
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	die_nest_count--;
	if (!die_nest_count)

		arch_spin_unlock(&die_lock);
	raw_local_irq_restore(flags);

	if (!signr)
		return;
	if (in_interrupt())
		panic("Fatal exception in interrupt");

	rewind_stack_and_make_dead(signr);
}
NOKPROBE_SYMBOL(oops_end);

int __die(const char *str, struct pt_regs *regs, long err)
{
	return 0;
}
NOKPROBE_SYMBOL(__die);

void die(const char *str, struct pt_regs *regs, long err)
{
	unsigned long flags = oops_begin();
	__die(str, regs, err);
	oops_end(flags, regs, SIGSEGV);
}
