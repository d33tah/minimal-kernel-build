// SPDX-License-Identifier: GPL-2.0-only
/*
 *	linux/kernel/softirq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 *	Rewritten. Old one was good in 2.2, but in 2.3 it was immoral. --ANK (990903)
 */

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

/**
 * irq_exit_rcu() - Exit an interrupt context without updating RCU
 *
 * Also processes softirqs if needed and possible.
 */
void irq_exit_rcu(void)
{
	__irq_exit_rcu();
}
