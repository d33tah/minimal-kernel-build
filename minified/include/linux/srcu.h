
#ifndef _LINUX_SRCU_H
#define _LINUX_SRCU_H

#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>

/* --- Inlined from rcu_segcblist.h (2025-12-08 02:00) --- */
#include <linux/types.h>
#include <linux/atomic.h>


#define RCU_DONE_TAIL		0
#define RCU_WAIT_TAIL		1
#define RCU_NEXT_READY_TAIL	2
#define RCU_NEXT_TAIL		3
#define RCU_CBLIST_NSEGS	4

struct rcu_segcblist {
	struct rcu_head *head;
	struct rcu_head **tails[RCU_CBLIST_NSEGS];
	unsigned long gp_seq[RCU_CBLIST_NSEGS];
	long len;
	long seglen[RCU_CBLIST_NSEGS];
	u8 flags;
};

#define RCU_SEGCBLIST_INITIALIZER(n) \
{ \
	.head = NULL, \
	.tails[RCU_DONE_TAIL] = &n.head, \
	.tails[RCU_WAIT_TAIL] = &n.head, \
	.tails[RCU_NEXT_READY_TAIL] = &n.head, \
	.tails[RCU_NEXT_TAIL] = &n.head, \
}

struct srcu_struct;


int init_srcu_struct(struct srcu_struct *ssp);

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


void call_srcu(struct srcu_struct *ssp, struct rcu_head *head,
		void (*func)(struct rcu_head *head));
void cleanup_srcu_struct(struct srcu_struct *ssp);
int __srcu_read_lock(struct srcu_struct *ssp) __acquires(ssp);
void __srcu_read_unlock(struct srcu_struct *ssp, int idx) __releases(ssp);
void synchronize_srcu(struct srcu_struct *ssp);
unsigned long get_state_synchronize_srcu(struct srcu_struct *ssp);
unsigned long start_poll_synchronize_srcu(struct srcu_struct *ssp);
bool poll_state_synchronize_srcu(struct srcu_struct *ssp, unsigned long cookie);

void srcu_init(void);


static inline int srcu_read_lock_held(const struct srcu_struct *ssp)
{
	return 1;
}


#define srcu_dereference_check(p, ssp, c) \
	__rcu_dereference_check((p), __UNIQUE_ID(rcu), \
				(c) || srcu_read_lock_held(ssp), __rcu)

#define srcu_dereference(p, ssp) srcu_dereference_check((p), (ssp), 0)

#define srcu_dereference_notrace(p, ssp) srcu_dereference_check((p), (ssp), 1)

static inline int srcu_read_lock(struct srcu_struct *ssp) __acquires(ssp)
{
	int retval;

	retval = __srcu_read_lock(ssp);
	rcu_lock_acquire(&(ssp)->dep_map);
	return retval;
}


static inline void srcu_read_unlock(struct srcu_struct *ssp, int idx)
	__releases(ssp)
{
	WARN_ON_ONCE(idx & ~0x1);
	rcu_lock_release(&(ssp)->dep_map);
	__srcu_read_unlock(ssp, idx);
}


#endif
