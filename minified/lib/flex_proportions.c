/* Minimal stub for flex_proportions - used by backing-dev.c */
#include <linux/flex_proportions.h>
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
