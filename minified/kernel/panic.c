/* debug_locks.h, kallsyms.h, module.h, ratelimit.h, asm/sections.h removed - unused */
#include <linux/sched/debug.h>
#include <linux/printk.h>
#include <linux/kdebug.h> /* for notify_die */
#include <linux/kprobes.h> /* for NOKPROBE_SYMBOL */

/* Merged from lib/debug_locks.c */
int debug_locks __read_mostly = 1;
int debug_locks_silent __read_mostly;
/* kmsg_dump removed - was empty inline stub */
#include <linux/notifier.h>
#include <linux/vt_kern.h>
#include <linux/reboot.h>
#include <linux/delay.h>
/* panic_notifier_list extern removed - no registrations */
#include <linux/sched.h>

#include <linux/init.h>
#include <linux/console.h>
#include <linux/bug.h>

#define PANIC_TIMER_STEP 100
/* PANIC_BLINK_SPD removed - unused after panic_blink removal */

/* panic_on_oops removed - CONFIG_PANIC_ON_OOPS_VALUE=0, sysctl disabled */
/* tainted_mask removed - write-only, never read */
/* pause_on_oops, panic_on_warn removed - always 0, sysctl not available */
/* panic_on_taint removed - never set to non-zero */

int panic_timeout = CONFIG_PANIC_TIMEOUT;

/* ATOMIC_NOTIFIER_HEAD(panic_notifier_list) removed - never registered into */

/* panic_blink removed - no_blink always returned 0 */

void __weak panic_smp_self_stop(void)
{
	while (1)
		cpu_relax();
}

/* nmi_panic_self_stop removed - only called by nmi_panic which was removed */
/* crash_smp_send_stop removed - stub that only called smp_send_stop() which is empty */

atomic_t panic_cpu = ATOMIC_INIT(PANIC_CPU_INVALID);

/* nmi_panic removed - never called from outside panic.c */

void panic(const char *fmt, ...)
{
	static char buf[1024];
	va_list args;
	long i, len;
	int old_cpu, this_cpu;

	/* panic_on_warn check removed - always 0 */
	local_irq_disable();
	preempt_disable_notrace();

	this_cpu = raw_smp_processor_id();
	old_cpu = atomic_cmpxchg(&panic_cpu, PANIC_CPU_INVALID, this_cpu);

	if (old_cpu != PANIC_CPU_INVALID && old_cpu != this_cpu)
		panic_smp_self_stop();

	console_verbose();
	bust_spinlocks(1);
	va_start(args, fmt);
	len = vscnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (len && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	pr_emerg("Kernel panic - not syncing: %s\n", buf);

	/* atomic_notifier_call_chain removed - no registrations, always returns NOTIFY_DONE */

	/* kmsg_dump call removed - was empty */

	/* unblank_screen removed - was empty stub */
	console_unblank();

	debug_locks_off();
	console_flush_on_panic(CONSOLE_FLUSH_PENDING);

	if (panic_timeout > 0) {
		pr_emerg("Rebooting in %d seconds..\n", panic_timeout);

		for (i = 0; i < panic_timeout * 1000; i += PANIC_TIMER_STEP)
			mdelay(PANIC_TIMER_STEP);
	}
	if (panic_timeout != 0) {
		emergency_restart();
	}
	/* sparc-specific Stop-A handling removed - x86 only */
	pr_emerg("---[ end Kernel panic - not syncing: %s ]---\n", buf);

	local_irq_enable();
	for (;;)
		mdelay(PANIC_TIMER_STEP);
}

void add_taint(unsigned flag, enum lockdep_ok lockdep_ok)
{
	if (lockdep_ok == LOCKDEP_NOW_UNRELIABLE)
		__debug_locks_off();

	/* set_bit(flag, &tainted_mask) removed - tainted_mask never read */
	/* panic_on_taint check removed - never set to non-zero */
}

/* spin_msec, do_oops_enter_exit removed - pause_on_oops always 0 */
/* oops_enter, oops_exit inlined into dumpstack.c - single caller */

/* Merged from lib/debug_locks.c */
int debug_locks_off(void)
{
	if (debug_locks && __debug_locks_off()) {
		if (!debug_locks_silent) {
			console_verbose();
			return 1;
		}
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

struct warn_args {
	const char *fmt;
	va_list args;
};

void __warn(const char *file, int line, void *caller, unsigned taint,
	    struct pt_regs *regs, struct warn_args *args)
{
	/* Stub: panic_on_warn check removed (always 0) */
	add_taint(taint, LOCKDEP_STILL_OK);
}

/* machine_restart inlined - just calls halt() in a loop */
void emergency_restart(void)
{
	/* kmsg_dump call removed - was empty */
	while (1)
		halt();
}

/* atomic_notifier_call_chain removed - no registrations, always returns NOTIFY_DONE */

int notrace notify_die(enum die_val val, const char *str, struct pt_regs *regs,
		       long err, int trap, int sig)
{
	return NOTIFY_DONE;
}
NOKPROBE_SYMBOL(notify_die);
