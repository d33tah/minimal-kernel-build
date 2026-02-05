
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/local_lock.h>
#include <linux/percpu.h>
/* linux/cpu.h removed - cpu hotplug not used */
#include <linux/rcupdate.h>
#include <linux/smp.h>
#include <linux/irq.h>
/* linux/wait_bit.h removed - not used */
void do_softirq_own_stack(void);

#ifndef __ARCH_IRQ_STAT
DEFINE_PER_CPU_ALIGNED(irq_cpustat_t, irq_stat);
#endif

static struct softirq_action softirq_vec[NR_SOFTIRQS] __cacheline_aligned_in_smp;

/* ksoftirqd, wakeup_softirqd, ksoftirqd_running, SOFTIRQ_NOW_MASK removed -
 * ksoftirqd was never assigned (spawn_ksoftirqd was removed), so it's always
 * NULL and all the code that checks it is dead */

void __local_bh_enable_ip(unsigned long ip, unsigned int cnt)
{
	WARN_ON_ONCE(in_hardirq());
	__preempt_count_sub(cnt - 1);

	if (unlikely(!in_interrupt() && local_softirq_pending())) {
		do_softirq();
	}

	preempt_count_dec();
}

/* ksoftirqd_run_begin/end removed - only caller was run_ksoftirqd */
/* should_wake_ksoftirqd, invoke_softirq - inlined/removed */
/* softirq_handle_begin/end inlined into __do_softirq */

asmlinkage __visible void do_softirq(void)
{
	__u32 pending;
	unsigned long flags;

	if (in_interrupt())
		return;

	local_irq_save(flags);

	pending = local_softirq_pending();

	/* ksoftirqd_running always returned false (ksoftirqd was never set) */
	if (pending)
		do_softirq_own_stack();

	local_irq_restore(flags);
}

/* Simplified for minimal kernel - no time-based limiting */
#define MAX_SOFTIRQ_RESTART 10

asmlinkage __visible void __softirq_entry __do_softirq(void)
{
	unsigned long old_flags = current->flags;
	int max_restart = MAX_SOFTIRQ_RESTART;
	struct softirq_action *h;
	__u32 pending;
	int softirq_bit;

	current->flags &= ~PF_MEMALLOC;

	pending = local_softirq_pending();

	__local_bh_disable_ip(_RET_IP_, SOFTIRQ_OFFSET);

restart:

	set_softirq_pending(0);

	local_irq_enable();

	h = softirq_vec;

	while ((softirq_bit = ffs(pending))) {
		unsigned int vec_nr;
		int prev_count;

		h += softirq_bit - 1;

		vec_nr = h - softirq_vec;
		prev_count = preempt_count();
		/* kstat_incr_softirqs_this_cpu call removed - empty stub */
		h->action(h);

		if (unlikely(prev_count != preempt_count()))
			preempt_count_set(prev_count);
		h++;
		pending >>= softirq_bit;
	}

	/* ksoftirqd check removed - ksoftirqd is always NULL */

	local_irq_disable();

	pending = local_softirq_pending();
	if (pending) {
		/* Simplified: only count-based restart, no time checking */
		if (!need_resched() && --max_restart)
			goto restart;
		/* wakeup_softirqd removed - does nothing since ksoftirqd is NULL */
	}

	__preempt_count_sub(SOFTIRQ_OFFSET);
	WARN_ON_ONCE(in_interrupt());
	current_restore_flags(old_flags, PF_MEMALLOC);
}

void irq_enter_rcu(void)
{
	__irq_enter_raw();
}

void irq_enter(void)
{
	irq_enter_rcu();
}

static inline void __irq_exit_rcu(void)
{
	/* __ARCH_IRQ_EXIT_IRQS_DISABLED not defined for x86 */
	local_irq_disable();
	preempt_count_sub(HARDIRQ_OFFSET);
	if (!in_interrupt() && local_softirq_pending()) {
		/* ksoftirqd_running always returns false, force_irqthreads always false */
		do_softirq_own_stack();
	}
}

void irq_exit_rcu(void)
{
	__irq_exit_rcu();
	/* lockdep_hardirq_exit is empty do{}while(0) */
}

void irq_exit(void)
{
	__irq_exit_rcu();
	/* rcu_irq_exit, lockdep_hardirq_exit are empty stubs */
}

inline void raise_softirq_irqoff(unsigned int nr)
{
	__raise_softirq_irqoff(nr);
	/* wakeup_softirqd removed - does nothing since ksoftirqd is NULL */
}

void __raise_softirq_irqoff(unsigned int nr)
{
	or_softirq_pending(1UL << nr);
}

void open_softirq(int nr, void (*action)(struct softirq_action *))
{
	softirq_vec[nr].action = action;
}

/* softirq_init removed - was empty stub (no tasklets needed) */
/* ksoftirqd_should_run, run_ksoftirqd, spawn_ksoftirqd removed -
   CPU never goes offline, so no takeover_tasklets needed (~8 LOC) */

/* early_irq_init removed - non-weak version exists in kernel/irq/irqdesc.c
 * arch_probe_nr_irqs, arch_early_irq_init, arch_dynirq_lower_bound removed - inlined at call sites */
