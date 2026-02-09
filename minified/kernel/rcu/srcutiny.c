
/* linux/export.h removed - no EXPORT_SYMBOL */
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

/* call_srcu, synchronize_srcu, get_state_synchronize_srcu removed - no callers */

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
