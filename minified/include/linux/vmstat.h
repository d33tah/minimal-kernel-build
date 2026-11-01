/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMSTAT_H
#define _LINUX_VMSTAT_H

static inline void zone_page_state_add(long x, struct zone *zone,
				       enum zone_stat_item item)
{
	atomic_long_add(x, &zone->vm_stat[item]);
}

/*
 * We do not maintain differentials in a single processor configuration.
 * The functions directly modify the zone and global counters.
 */
static inline void __mod_zone_page_state(struct zone *zone,
					 enum zone_stat_item item, long delta)
{
	zone_page_state_add(delta, zone, item);
}

#endif
