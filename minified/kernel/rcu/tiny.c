#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/rcupdate_wait.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/cpu.h>

#include <linux/slab.h>
#include <linux/mm.h>

#include "rcu.h"

struct rcu_ctrlblk {
	struct rcu_head *rcucblist;
	struct rcu_head **donetail;
	struct rcu_head **curtail;
	unsigned long gp_seq;
};

static struct rcu_ctrlblk rcu_ctrlblk = {
	.donetail = &rcu_ctrlblk.rcucblist,
	.curtail = &rcu_ctrlblk.rcucblist,
	.gp_seq = 0 - 300UL,
};

void rcu_barrier(void)
{
	wait_rcu_gp(call_rcu);
}

void rcu_qs(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (rcu_ctrlblk.donetail != rcu_ctrlblk.curtail) {
		rcu_ctrlblk.donetail = rcu_ctrlblk.curtail;
		raise_softirq_irqoff(RCU_SOFTIRQ);
	}
	WRITE_ONCE(rcu_ctrlblk.gp_seq, rcu_ctrlblk.gp_seq + 1);
	local_irq_restore(flags);
}

void rcu_sched_clock_irq(int user)
{
	if (user) {
		rcu_qs();
	} else if (rcu_ctrlblk.donetail != rcu_ctrlblk.curtail) {
		set_tsk_need_resched(current);
		set_preempt_need_resched();
	}
}

/* rcu_reclaim_tiny inlined into rcu_process_callbacks */

static __latent_entropy void
rcu_process_callbacks(struct softirq_action *unused)
{
	struct rcu_head *next, *list;
	unsigned long flags;

	local_irq_save(flags);
	if (rcu_ctrlblk.donetail == &rcu_ctrlblk.rcucblist) {
		local_irq_restore(flags);
		return;
	}
	list = rcu_ctrlblk.rcucblist;
	rcu_ctrlblk.rcucblist = *rcu_ctrlblk.donetail;
	*rcu_ctrlblk.donetail = NULL;
	if (rcu_ctrlblk.curtail == rcu_ctrlblk.donetail)
		rcu_ctrlblk.curtail = &rcu_ctrlblk.rcucblist;
	rcu_ctrlblk.donetail = &rcu_ctrlblk.rcucblist;
	local_irq_restore(flags);

	while (list) {
		next = list->next;
		prefetch(next);
		/* debug_rcu_head_unqueue removed - empty stub */
		local_bh_disable();
		/* rcu_reclaim_tiny inlined */
		{
			unsigned long offset = (unsigned long)list->func;
			if (__is_kvfree_rcu_offset(offset)) {
				kvfree((void *)list - offset);
			} else {
				rcu_callback_t f = list->func;
				WRITE_ONCE(list->func, (rcu_callback_t)0L);
				f(list);
			}
		}
		local_bh_enable();
		list = next;
	}
}

void synchronize_rcu(void)
{
}

void call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	unsigned long flags;

	/* debug_rcu_head_queue removed - empty stub */
	head->func = func;
	head->next = NULL;

	local_irq_save(flags);
	*rcu_ctrlblk.curtail = head;
	rcu_ctrlblk.curtail = &head->next;
	local_irq_restore(flags);

	/* is_idle_task inlined - single caller */
	if (unlikely(current->flags & PF_IDLE)) {
		resched_cpu(0);
	}
}

void __init rcu_init(void)
{
	open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
	/* rcu_early_boot_tests removed - empty stub */
}
