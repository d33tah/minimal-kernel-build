
#ifndef _LINUX_FLEX_PROPORTIONS_H
#define _LINUX_FLEX_PROPORTIONS_H

#include <linux/percpu_counter.h>
#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <linux/gfp.h>

#define FPROP_FRAC_SHIFT 10
#define FPROP_FRAC_BASE (1UL << FPROP_FRAC_SHIFT)

struct fprop_global {
	 
	struct percpu_counter events;
	 
	unsigned int period;
	 
	seqcount_t sequence;
};


struct fprop_local_percpu {
	 
	struct percpu_counter events;
	 
	unsigned int period;
	raw_spinlock_t lock;	 
};

int fprop_local_init_percpu(struct fprop_local_percpu *pl, gfp_t gfp);
void fprop_local_destroy_percpu(struct fprop_local_percpu *pl);

#endif
