#include <linux/interrupt.h>

/*
 * No softirq vector is left (RCU was the last one), so nothing raises a
 * softirq: local_bh_*, the pending mask, __do_softirq() and the softirq
 * stack are gone. Only the hardirq preempt-count accounting remains.
 */

void irq_enter_rcu(void)
{
	__irq_enter_raw();
}

static inline void __irq_exit_rcu(void)
{
	local_irq_disable();
	preempt_count_sub(HARDIRQ_OFFSET);
}

void irq_exit_rcu(void)
{
	__irq_exit_rcu();
}
