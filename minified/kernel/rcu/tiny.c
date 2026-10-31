#include <linux/interrupt.h>
#include <linux/slab.h>

#include "rcu.h"

struct rcu_ctrlblk {
	struct rcu_head *rcucblist, **donetail, **curtail;
	unsigned long gp_seq;
};

static struct rcu_ctrlblk rcu_ctrlblk = {
	.donetail = &rcu_ctrlblk.rcucblist,
	.curtail = &rcu_ctrlblk.rcucblist,
	.gp_seq = 0 - 300UL,
};

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

static inline bool rcu_reclaim_tiny(struct rcu_head *head)
{
	rcu_callback_t f;
	unsigned long offset = (unsigned long)head->func;

	if (__is_kvfree_rcu_offset(offset)) {
		kfree((void *)head - offset);
		return true;
	}

	f = head->func;
	WRITE_ONCE(head->func, (rcu_callback_t)0L);
	f(head);
	return false;
}

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
		local_bh_disable();
		rcu_reclaim_tiny(list);
		local_bh_enable();
		list = next;
	}
}

/*
 * Tiny RCU runs on one CPU without preemption, and rcu_barrier() may sleep, so
 * its caller is in a quiescent state: every callback queued so far has
 * finished its grace period. Mark them done and invoke them before returning.
 * The generic wait_rcu_gp() version queued an on-stack rcu_head and relied on
 * wait_for_completion(), which is a no-op here, so it returned with the head
 * still queued and the RCU softirq later called garbage from the dead frame.
 */
void rcu_barrier(void)
{
	rcu_qs();
	rcu_process_callbacks(NULL);
}

void call_rcu(struct rcu_head *head, rcu_callback_t func)
{
	unsigned long flags;

	head->func = func;
	head->next = NULL;

	local_irq_save(flags);
	*rcu_ctrlblk.curtail = head;
	rcu_ctrlblk.curtail = &head->next;
	local_irq_restore(flags);

	if (unlikely(is_idle_task(current))) {
		resched_cpu(0);
	}
}

void __init rcu_init(void)
{
	open_softirq(RCU_SOFTIRQ, rcu_process_callbacks);
}
