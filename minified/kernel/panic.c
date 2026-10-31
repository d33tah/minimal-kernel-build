
#include <linux/delay.h>

#define PANIC_TIMER_STEP 100

void panic(const char *fmt, ...)
{
	local_irq_disable();
	preempt_disable_notrace();

	/*
	 * The panic message was formatted only for pr_emerg() (a no-op without
	 * printk) and the panic notifier chain (never populated), so it is not
	 * built at all; there is no log to dump or console to flush either.
	 */
	local_irq_enable();
	for (;;)
		mdelay(PANIC_TIMER_STEP);
}

/* Removed: oops_may_print - never called */
/* Removed: oops_enter - 0-caller (sole caller oops_begin stubbed tick #320) */
/* Removed: add_taint + oops_exit - 0-caller after oops_end anchor-stubbed (tick #322) */
