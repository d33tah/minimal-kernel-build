// SPDX-License-Identifier: GPL-2.0
// Stubbed vmscan.c - page reclaim and memory scanning not needed for minimal kernel

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/swap.h>
#include <linux/pagemap.h>

// Shrinker registration - stub implementation
int register_shrinker(struct shrinker *shrinker)
{
	return 0;  // Success, but do nothing
}

void unregister_shrinker(struct shrinker *shrinker)
{
	// Do nothing
}

void synchronize_shrinkers(void)
{
	// Do nothing
}

void check_move_unevictable_pages(struct pagevec *pvec)
{
	// Do nothing - no page eviction needed
}

// Additional internal functions needed by the kernel
int prealloc_shrinker(struct shrinker *shrinker)
{
	return 0;  // Success
}

void free_prealloced_shrinker(struct shrinker *shrinker)
{
	// Do nothing
}

void register_shrinker_prepared(struct shrinker *shrinker)
{
	// Do nothing
}

unsigned long zone_reclaimable_pages(struct zone *zone)
{
	return 0;  // No reclaimable pages
}

unsigned long try_to_free_pages(struct zonelist *zonelist, int order,
				 gfp_t gfp_mask, nodemask_t *nodemask)
{
	return 0;  // Can't free any pages
}

void wakeup_kswapd(struct zone *zone, gfp_t gfp_flags, int order,
		   enum zone_type highest_zoneidx)
{
	// Do nothing - no kswapd daemon
}

void __acct_reclaim_writeback(pg_data_t *pgdat, struct folio *folio,
			       int nr_throttled)
{
	// Do nothing
}

void reclaim_throttle(pg_data_t *pgdat, enum vmscan_throttle_state reason)
{
	// Do nothing
}

long remove_mapping(struct address_space *mapping, struct folio *folio)
{
	return 0;  // Can't remove
}

void folio_putback_lru(struct folio *folio)
{
	// Do nothing
}

int folio_isolate_lru(struct folio *folio)
{
	return -1;  // Can't isolate
}
