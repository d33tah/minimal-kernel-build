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
