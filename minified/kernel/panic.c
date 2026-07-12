
#include <linux/debug_locks.h>
#include <linux/printk.h>
#include <linux/kmsg_dump.h>
#include <linux/notifier.h>
#include <linux/vt_kern.h>
#include <linux/delay.h>
#include <linux/console.h>

#define PANIC_TIMER_STEP 100
#define PANIC_BLINK_SPD 18

ATOMIC_NOTIFIER_HEAD(panic_notifier_list);



static long no_blink(int state) {
	return 0;
}

long (*panic_blink)(int state);

void __weak panic_smp_self_stop(void) {
	while (1)
		cpu_relax();
}

atomic_t panic_cpu = ATOMIC_INIT(PANIC_CPU_INVALID);

void panic(const char *fmt, ...) {
	static char buf[1024];
	va_list args;
	long i, i_next = 0, len;
	int state = 0;
	int old_cpu, this_cpu;

	local_irq_disable();
	preempt_disable_notrace();

	 
	this_cpu = raw_smp_processor_id();
	old_cpu  = atomic_cmpxchg(&panic_cpu, PANIC_CPU_INVALID, this_cpu);

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

	unblank_screen();
	console_unblank();

	 
	debug_locks_off();
	console_flush_on_panic(CONSOLE_FLUSH_PENDING);

	if (!panic_blink)
		panic_blink = no_blink;

	pr_emerg("---[ end Kernel panic - not syncing: %s ]---\n", buf);

	local_irq_enable();
	for (i = 0; ; i += PANIC_TIMER_STEP) {
		if (i >= i_next) {
			i += panic_blink(state ^= 1);
			i_next = i + 3600 / PANIC_BLINK_SPD;
		}
		mdelay(PANIC_TIMER_STEP);
	}
}




/* Removed: oops_may_print - never called */
/* Removed: oops_enter - 0-caller (sole caller oops_begin stubbed tick #320) */
/* Removed: add_taint + oops_exit - 0-caller after oops_end anchor-stubbed (tick #322) */






