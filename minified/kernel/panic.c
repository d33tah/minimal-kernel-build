#include <linux/sched.h>

/*
 * MKB_PANIC_HALT: noinline keeps panic() one function that a debugger can
 * break on; upstream's body is large enough that it never gets inlined, the
 * halt loop below alone would be inlined into every caller under LTO.
 */
noinline void panic(const char *fmt, ...)
{
	local_irq_disable();
	preempt_disable_notrace();

	/*
	 * The panic message was formatted only for pr_emerg() (a no-op without
	 * printk) and the panic notifier chain (never populated), so it is not
	 * built at all; there is no log to dump or console to flush either.
	 */
	/*
	 * MKB_PANIC_HALT: upstream re-enables interrupts and spins in
	 * mdelay(PANIC_TIMER_STEP) forever to blink the keyboard LEDs and keep
	 * the softlockup/NMI watchdogs fed; this kernel has no LEDs, watchdogs
	 * or panic reboot, so the CPU halts with interrupts off instead, in
	 * upstream stop_this_cpu()'s loop.
	 */
	for (;;) {
		native_halt();
	}
}
