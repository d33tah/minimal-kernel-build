/* Minimal stub for flex_proportions - flexible proportions library
 * Used only by backing-dev.c for writeback tracking
 * Original: 202 LOC, Stubbed to minimal implementations
 */

#include <linux/flex_proportions.h>

int fprop_global_init(struct fprop_global *p, gfp_t gfp)
{
	p->period = 0;
	seqcount_init(&p->sequence);
	return percpu_counter_init(&p->events, 1, gfp);
}

void fprop_global_destroy(struct fprop_global *p)
{
	percpu_counter_destroy(&p->events);
}

bool fprop_new_period(struct fprop_global *p, int periods)
{
	return false;
}

/* Stub: single-cpu variants not used in minimal kernel */
int fprop_local_init_single(struct fprop_local_single *pl) { return 0; }
void fprop_local_destroy_single(struct fprop_local_single *pl) { }
void __fprop_inc_single(struct fprop_global *p, struct fprop_local_single *pl) { }
void fprop_fraction_single(struct fprop_global *p,
			   struct fprop_local_single *pl,
			   unsigned long *numerator, unsigned long *denominator)
{ *numerator = 0; *denominator = 1; }

int fprop_local_init_percpu(struct fprop_local_percpu *pl, gfp_t gfp)
{
	int err = percpu_counter_init(&pl->events, 0, gfp);
	if (err)
		return err;
	pl->period = 0;
	raw_spin_lock_init(&pl->lock);
	return 0;
}

void fprop_local_destroy_percpu(struct fprop_local_percpu *pl)
{
	percpu_counter_destroy(&pl->events);
}

void __fprop_add_percpu(struct fprop_global *p, struct fprop_local_percpu *pl,
		long nr)
{
	percpu_counter_add(&pl->events, nr);
	percpu_counter_add(&p->events, nr);
}

void fprop_fraction_percpu(struct fprop_global *p,
			   struct fprop_local_percpu *pl,
			   unsigned long *numerator, unsigned long *denominator)
{
	s64 num = percpu_counter_read_positive(&pl->events);
	s64 den = percpu_counter_read_positive(&p->events);

	if (den <= num) {
		den = num ? num : 1;
	}
	*denominator = den;
	*numerator = num;
}

void __fprop_add_percpu_max(struct fprop_global *p,
		struct fprop_local_percpu *pl, int max_frac, long nr)
{
	__fprop_add_percpu(p, pl, nr);
}
