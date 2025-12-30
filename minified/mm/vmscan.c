/* Minimal vmscan.c - only remove_mapping needed */
#include <linux/mm.h>
#include <linux/pagemap.h>

/* register_shrinker, unregister_shrinker, prealloc_shrinker,
   free_prealloced_shrinker, register_shrinker_prepared,
   __acct_reclaim_writeback removed - callers removed */

long remove_mapping(struct address_space *mapping, struct folio *folio)
{
	return 0;
}
