#ifndef _LINUX_PERCPU_COUNTER_H
#define _LINUX_PERCPU_COUNTER_H

/* linux/spinlock.h, linux/list.h removed - unused */
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/gfp.h>


struct percpu_counter {
	s64 count;
};

static inline int percpu_counter_init(struct percpu_counter *fbc, s64 amount,
				      gfp_t gfp)
{
	fbc->count = amount;
	return 0;
}

static inline void percpu_counter_destroy(struct percpu_counter *fbc)
{
}


static inline void
percpu_counter_add(struct percpu_counter *fbc, s64 amount)
{
	preempt_disable();
	fbc->count += amount;
	preempt_enable();
}

static inline void
percpu_counter_add_batch(struct percpu_counter *fbc, s64 amount, s32 batch)
{
	percpu_counter_add(fbc, amount);
}

static inline s64 percpu_counter_read_positive(struct percpu_counter *fbc)
{
	return fbc->count;
}

/* percpu_counter_inc, percpu_counter_dec removed - never called */

#endif  
