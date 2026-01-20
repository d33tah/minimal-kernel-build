
#include <linux/debug_locks.h>
#include <linux/sched/debug.h>
#include <linux/printk.h>
#include <linux/kdebug.h> /* for notify_die */
#include <linux/kprobes.h> /* for NOKPROBE_SYMBOL */

/* Merged from lib/debug_locks.c */
int debug_locks __read_mostly = 1;
int debug_locks_silent __read_mostly;
#include <linux/kallsyms.h>
#include <linux/kmsg_dump.h>
#include <linux/notifier.h>
#include <linux/vt_kern.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/delay.h>
extern struct atomic_notifier_head panic_notifier_list;
#include <linux/sched.h>

#include <linux/init.h>
#include <linux/console.h>
#include <linux/bug.h>
#include <linux/ratelimit.h>
#include <asm/sections.h>

#define PANIC_TIMER_STEP 100
/* PANIC_BLINK_SPD removed - unused after panic_blink removal */

int panic_on_oops = CONFIG_PANIC_ON_OOPS_VALUE;
/* tainted_mask removed - write-only, never read */
/* pause_on_oops removed - always 0, sysctl not available */
int panic_on_warn __read_mostly;
/* panic_on_taint removed - never set to non-zero */

int panic_timeout = CONFIG_PANIC_TIMEOUT;

ATOMIC_NOTIFIER_HEAD(panic_notifier_list);

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

	if (panic_on_warn) {
		panic_on_warn = 0;
	}

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

	atomic_notifier_call_chain(&panic_notifier_list, 0, buf);

	kmsg_dump(KMSG_DUMP_PANIC);

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

void oops_enter(void)
{
	debug_locks_off();
}

void oops_exit(void)
{
	kmsg_dump(KMSG_DUMP_OOPS);
}

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
	/* Stub: minimal warning for tiny kernel */
	if (panic_on_warn)
		panic("panic_on_warn set ...\n");
	add_taint(taint, LOCKDEP_STILL_OK);
}

/* machine_restart inlined - just calls halt() in a loop */
void emergency_restart(void)
{
	kmsg_dump(KMSG_DUMP_EMERG);
	while (1)
		halt();
}

/* Merged from kernel/notifier.c */
int atomic_notifier_call_chain(struct atomic_notifier_head *nh,
			       unsigned long val, void *v)
{
	return NOTIFY_DONE; /* No registrations, chain is always empty */
}
NOKPROBE_SYMBOL(atomic_notifier_call_chain);

int notrace notify_die(enum die_val val, const char *str, struct pt_regs *regs,
		       long err, int trap, int sig)
{
	return NOTIFY_DONE;
}
NOKPROBE_SYMBOL(notify_die);
