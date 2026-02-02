
#ifndef _LINUX_SRCU_H
#define _LINUX_SRCU_H

#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>

/* --- Inlined from rcu_segcblist.h (2025-12-08 02:00) --- */
#include <linux/types.h>
#include <linux/atomic.h>

/* RCU_CBLIST_NSEGS, struct rcu_segcblist removed - unused */

struct srcu_struct;


/* init_srcu_struct removed - never called */

#define __SRCU_DEP_MAP_INIT(srcu_name)

/* Inlined from srcutiny.h */
#include <linux/swait.h>

struct srcu_struct {
	short srcu_lock_nesting[2];
	unsigned short srcu_idx;
	unsigned short srcu_idx_max;
	u8 srcu_gp_running;
	u8 srcu_gp_waiting;
	struct swait_queue_head srcu_wq;
	struct rcu_head *srcu_cb_head;
	struct rcu_head **srcu_cb_tail;
	struct work_struct srcu_work;
};

void srcu_drive_gp(struct work_struct *wp);

#define __SRCU_STRUCT_INIT(name, __ignored)				\
{									\
	.srcu_wq = __SWAIT_QUEUE_HEAD_INITIALIZER(name.srcu_wq),	\
	.srcu_cb_tail = &name.srcu_cb_head,				\
	.srcu_work = __WORK_INITIALIZER(name.srcu_work, srcu_drive_gp),	\
	__SRCU_DEP_MAP_INIT(name)					\
}

/* DEFINE_STATIC_SRCU removed - never used */

void synchronize_srcu(struct srcu_struct *ssp);

void call_srcu(struct srcu_struct *ssp, struct rcu_head *head,
		void (*func)(struct rcu_head *head));
/* cleanup_srcu_struct, start_poll_synchronize_srcu, poll_state_synchronize_srcu removed - never called */
/* __srcu_read_lock, __srcu_read_unlock, srcu_read_lock, srcu_read_unlock removed - never called */
unsigned long get_state_synchronize_srcu(struct srcu_struct *ssp);

void srcu_init(void);

#endif
