
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/swap.h>
#include <linux/pagemap.h>

int register_shrinker(struct shrinker *shrinker)
{
	return 0;   
}

void unregister_shrinker(struct shrinker *shrinker)
{
	 
}

void synchronize_shrinkers(void)
{
	 
}

void check_move_unevictable_pages(struct pagevec *pvec)
{
	 
}

int prealloc_shrinker(struct shrinker *shrinker)
{
	return 0;   
}

void free_prealloced_shrinker(struct shrinker *shrinker)
{
	 
}

void register_shrinker_prepared(struct shrinker *shrinker)
{
	 
}

unsigned long zone_reclaimable_pages(struct zone *zone)
{
	return 0;   
}

unsigned long try_to_free_pages(struct zonelist *zonelist, int order,
				 gfp_t gfp_mask, nodemask_t *nodemask)
{
	return 0;   
}

void wakeup_kswapd(struct zone *zone, gfp_t gfp_flags, int order,
		   enum zone_type highest_zoneidx)
{
	 
}

void __acct_reclaim_writeback(pg_data_t *pgdat, struct folio *folio,
			       int nr_throttled)
{
	 
}

void reclaim_throttle(pg_data_t *pgdat, enum vmscan_throttle_state reason)
{
	 
}

long remove_mapping(struct address_space *mapping, struct folio *folio)
{
	return 0;   
}

void folio_putback_lru(struct folio *folio)
{
	 
}

int folio_isolate_lru(struct folio *folio)
{
	return -1;   
}
