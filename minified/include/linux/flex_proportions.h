
#ifndef _LINUX_FLEX_PROPORTIONS_H
#define _LINUX_FLEX_PROPORTIONS_H

#include <linux/percpu_counter.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>

/* FPROP_FRAC_SHIFT, FPROP_FRAC_BASE, fprop_global removed - never used */

struct fprop_local_percpu {
	struct percpu_counter events;
	/* period removed - write-only, never read */
	raw_spinlock_t lock;
};

void fprop_local_init_percpu(struct fprop_local_percpu *pl, gfp_t gfp);
/* fprop_local_destroy_percpu declaration removed - inlined at single call site */

#endif
