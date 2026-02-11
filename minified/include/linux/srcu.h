
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

/* __SRCU_DEP_MAP_INIT removed - only user was __SRCU_STRUCT_INIT */

/* Inlined from srcutiny.h */
/* --- Inlined from swait.h --- */
#ifndef _LINUX_SWAIT_H
#define _LINUX_SWAIT_H

#include <linux/list.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <asm/current.h>

struct task_struct;

struct swait_queue_head {
	raw_spinlock_t		lock;
	struct list_head	task_list;
};

struct swait_queue {
	struct task_struct	*task;
	struct list_head	task_list;
};

#define __SWAIT_QUEUE_HEAD_INITIALIZER(name) {				\
	.lock		= __RAW_SPIN_LOCK_UNLOCKED(name.lock),		\
	.task_list	= LIST_HEAD_INIT((name).task_list),		\
}

extern void __init_swait_queue_head(struct swait_queue_head *q, const char *name,
				    struct lock_class_key *key);

#define init_swait_queue_head(q)				\
	do {							\
		static struct lock_class_key __key;		\
		__init_swait_queue_head((q), #q, &__key);	\
	} while (0)

#endif /* _LINUX_SWAIT_H */
/* --- End inlined swait.h --- */

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


/* __SRCU_STRUCT_INIT removed - never used */

/* DEFINE_STATIC_SRCU removed - never used */

/* synchronize_srcu, call_srcu, get_state_synchronize_srcu removed - no callers */

void srcu_init(void);

#endif
