 
 

#ifndef _LINUX_SRCU_TINY_H
#define _LINUX_SRCU_TINY_H

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

 
#define DEFINE_SRCU(name) \
	struct srcu_struct name = __SRCU_STRUCT_INIT(name, name)
#define DEFINE_STATIC_SRCU(name) \
	static struct srcu_struct name = __SRCU_STRUCT_INIT(name, name)

void synchronize_srcu(struct srcu_struct *ssp);

 
static inline int __srcu_read_lock(struct srcu_struct *ssp)
{
	int idx;

	idx = ((READ_ONCE(ssp->srcu_idx) + 1) & 0x2) >> 1;
	WRITE_ONCE(ssp->srcu_lock_nesting[idx], READ_ONCE(ssp->srcu_lock_nesting[idx]) + 1);
	return idx;
}

static inline void synchronize_srcu_expedited(struct srcu_struct *ssp)
{
	synchronize_srcu(ssp);
}

static inline void srcu_barrier(struct srcu_struct *ssp)
{
	synchronize_srcu(ssp);
}

 
static inline void srcu_torture_stats_print(struct srcu_struct *ssp,
					    char *tt, char *tf)
{
	int idx;

	idx = ((data_race(READ_ONCE(ssp->srcu_idx)) + 1) & 0x2) >> 1;
	pr_alert("%s%s Tiny SRCU per-CPU(idx=%d): (%hd,%hd)\n",
		 tt, tf, idx,
		 data_race(READ_ONCE(ssp->srcu_lock_nesting[!idx])),
		 data_race(READ_ONCE(ssp->srcu_lock_nesting[idx])));
}

#endif
