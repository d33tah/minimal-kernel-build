#include <asm/kdebug.h>
#ifndef NOKPROBE_SYMBOL
#define NOKPROBE_SYMBOL(fname) /* kprobes disabled */
#endif

/* Merged from lib/debug_locks.c */
int debug_locks __read_mostly = 1;
#include <linux/vt_kern.h>

#include <linux/console.h>

atomic_t panic_cpu = ATOMIC_INIT(PANIC_CPU_INVALID);

void panic(const char *fmt, ...)
{
	static char buf[1024];
	va_list args;
	int len;

	local_irq_disable();
	preempt_disable_notrace();

	bust_spinlocks(1);
	va_start(args, fmt);
	len = vscnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (len && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	pr_emerg("Kernel panic - not syncing: %s\n", buf);

	console_unblank();
	debug_locks_off();

	pr_emerg("---[ end Kernel panic - not syncing: %s ]---\n", buf);

	local_irq_enable();
	for (;;)
		cpu_relax();
}

void add_taint(unsigned flag, enum lockdep_ok lockdep_ok)
{
	if (lockdep_ok == LOCKDEP_NOW_UNRELIABLE)
		__debug_locks_off();
}

/* Merged from lib/debug_locks.c */
int debug_locks_off(void)
{
	if (debug_locks && __debug_locks_off()) {
		console_verbose();
		return 1;
	}
	return 0;
}

/* Merged from lib/bust_spinlocks.c */
void bust_spinlocks(int yes)
{
	if (yes) {
		++oops_in_progress;
	} else {
		console_unblank();
		--oops_in_progress;
	}
}

int notrace notify_die(enum die_val val, const char *str, struct pt_regs *regs,
		       long err, int trap, int sig)
{
	return NOTIFY_DONE;
}
NOKPROBE_SYMBOL(notify_die);
