
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/preempt.h>
#include <linux/rcupdate_wait.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/srcu.h>

#include "rcu.h"

/* rcu_scheduler_active removed - write-only variable */
static LIST_HEAD(srcu_boot_list);
static bool srcu_init_done;

/* init_srcu_struct, cleanup_srcu_struct, __srcu_read_unlock removed - never called */

void srcu_drive_gp(struct work_struct *wp)
{
	int idx;
	struct rcu_head *lh;
	struct rcu_head *rhp;
	struct srcu_struct *ssp;

	ssp = container_of(wp, struct srcu_struct, srcu_work);
	if (ssp->srcu_gp_running ||
	    USHORT_CMP_GE(ssp->srcu_idx, READ_ONCE(ssp->srcu_idx_max)))
		return;

	WRITE_ONCE(ssp->srcu_gp_running, true);
	local_irq_disable();
	lh = ssp->srcu_cb_head;
	ssp->srcu_cb_head = NULL;
	ssp->srcu_cb_tail = &ssp->srcu_cb_head;
	local_irq_enable();
	idx = (ssp->srcu_idx & 0x2) / 2;
	WRITE_ONCE(ssp->srcu_idx, ssp->srcu_idx + 1);
	WRITE_ONCE(ssp->srcu_gp_waiting, true);
	swait_event_exclusive(ssp->srcu_wq,
			      !READ_ONCE(ssp->srcu_lock_nesting[idx]));
	WRITE_ONCE(ssp->srcu_gp_waiting, false);
	WRITE_ONCE(ssp->srcu_idx, ssp->srcu_idx + 1);

	while (lh) {
		rhp = lh;
		lh = lh->next;
		local_bh_disable();
		rhp->func(rhp);
		local_bh_enable();
	}

	WRITE_ONCE(ssp->srcu_gp_running, false);
	if (USHORT_CMP_LT(ssp->srcu_idx, READ_ONCE(ssp->srcu_idx_max)))
		schedule_work(&ssp->srcu_work);
}

void call_srcu(struct srcu_struct *ssp, struct rcu_head *rhp,
	       rcu_callback_t func)
{
	unsigned long flags;
	unsigned short cookie;

	rhp->func = func;
	rhp->next = NULL;
	local_irq_save(flags);
	*ssp->srcu_cb_tail = rhp;
	ssp->srcu_cb_tail = &rhp->next;
	local_irq_restore(flags);

	cookie = get_state_synchronize_srcu(ssp);
	if (USHORT_CMP_GE(READ_ONCE(ssp->srcu_idx_max), cookie))
		return;
	WRITE_ONCE(ssp->srcu_idx_max, cookie);
	if (!READ_ONCE(ssp->srcu_gp_running)) {
		if (likely(srcu_init_done))
			schedule_work(&ssp->srcu_work);
		else if (list_empty(&ssp->srcu_work.entry))
			list_add(&ssp->srcu_work.entry, &srcu_boot_list);
	}
}

void synchronize_srcu(struct srcu_struct *ssp)
{
	struct rcu_synchronize rs;

	init_completion(&rs.completion);
	call_srcu(ssp, &rs.head, wakeme_after_rcu);
	wait_for_completion(&rs.completion);
}

unsigned long get_state_synchronize_srcu(struct srcu_struct *ssp)
{
	unsigned long ret;

	barrier();
	ret = (READ_ONCE(ssp->srcu_idx) + 3) & ~0x1;
	barrier();
	return ret & USHRT_MAX;
}

/* start_poll_synchronize_srcu, poll_state_synchronize_srcu removed - never called */
/* rcu_scheduler_starting made inline in rcupdate.h - was empty stub */

void __init srcu_init(void)
{
	struct srcu_struct *ssp;

	srcu_init_done = true;
	while (!list_empty(&srcu_boot_list)) {
		ssp = list_first_entry(&srcu_boot_list, struct srcu_struct,
				       srcu_work.entry);
		list_del_init(&ssp->srcu_work.entry);
		schedule_work(&ssp->srcu_work);
	}
}
