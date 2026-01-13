
#include <linux/debug_locks.h>
#include <linux/sched/debug.h>
#include <linux/printk.h>
#include <linux/kallsyms.h>
#include <linux/kmsg_dump.h>
/* kgdb_panic removed - empty stub */
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
#define PANIC_BLINK_SPD 18

/* sysctl_oops_all_cpu_backtrace macro removed - unused */

int panic_on_oops = CONFIG_PANIC_ON_OOPS_VALUE;
static unsigned long tainted_mask = 0;
/* pause_on_oops removed - always 0, sysctl not available */
int panic_on_warn __read_mostly;
unsigned long panic_on_taint;

int panic_timeout = CONFIG_PANIC_TIMEOUT;

/* PANIC_PRINT_* macros and panic_print variable removed - never used */

ATOMIC_NOTIFIER_HEAD(panic_notifier_list);

/* panic_blink removed - no_blink always returned 0 */

void __weak panic_smp_self_stop(void)
{
	while (1)
		cpu_relax();
}

void __weak nmi_panic_self_stop(struct pt_regs *regs)
{
	panic_smp_self_stop();
}

/* crash_smp_send_stop removed - stub that only called smp_send_stop() which is empty */

atomic_t panic_cpu = ATOMIC_INIT(PANIC_CPU_INVALID);

void nmi_panic(struct pt_regs *regs, const char *msg)
{
	int old_cpu, cpu;

	cpu = raw_smp_processor_id();
	old_cpu = atomic_cmpxchg(&panic_cpu, PANIC_CPU_INVALID, cpu);

	if (old_cpu == PANIC_CPU_INVALID)
		panic("%s", msg);
	else if (old_cpu != cpu)
		nmi_panic_self_stop(regs);
}

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
	/* __crash_kexec, smp_send_stop, crash_smp_send_stop removed - stubs */

	atomic_notifier_call_chain(&panic_notifier_list, 0, buf);
	/* panic_print_sys_info removed - empty stub */

	kmsg_dump(KMSG_DUMP_PANIC);

	unblank_screen();
	console_unblank();

	debug_locks_off();
	console_flush_on_panic(CONSOLE_FLUSH_PENDING);

	if (panic_timeout > 0) {
		pr_emerg("Rebooting in %d seconds..\n", panic_timeout);

		for (i = 0; i < panic_timeout * 1000; i += PANIC_TIMER_STEP)
			mdelay(PANIC_TIMER_STEP);
	}
	if (panic_timeout != 0) {
		/* reboot_mode assignment removed - variable unused */
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

	set_bit(flag, &tainted_mask);

	if (tainted_mask & panic_on_taint) {
		panic_on_taint = 0;
		panic("panic_on_taint set ...");
	}
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
