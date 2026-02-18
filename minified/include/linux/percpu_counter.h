#ifndef _LINUX_PERCPU_COUNTER_H
#define _LINUX_PERCPU_COUNTER_H

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

#endif  
