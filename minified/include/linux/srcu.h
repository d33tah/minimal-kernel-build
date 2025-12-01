
#ifndef _LINUX_SRCU_H
#define _LINUX_SRCU_H

#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/workqueue.h>
#include <linux/rcu_segcblist.h>

struct srcu_struct;


int init_srcu_struct(struct srcu_struct *ssp);

#define __SRCU_DEP_MAP_INIT(srcu_name)

#include <linux/srcutiny.h>

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

static inline notrace int
srcu_read_lock_notrace(struct srcu_struct *ssp) __acquires(ssp)
{
	int retval;

	retval = __srcu_read_lock(ssp);
	return retval;
}

static inline void srcu_read_unlock(struct srcu_struct *ssp, int idx)
	__releases(ssp)
{
	WARN_ON_ONCE(idx & ~0x1);
	rcu_lock_release(&(ssp)->dep_map);
	__srcu_read_unlock(ssp, idx);
}

static inline notrace void
srcu_read_unlock_notrace(struct srcu_struct *ssp, int idx) __releases(ssp)
{
	__srcu_read_unlock(ssp, idx);
}

static inline void smp_mb__after_srcu_read_unlock(void)
{
	 
}

#endif
