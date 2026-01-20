

#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/mmzone.h>

/* Single-node kernel: first_online_node==0, next_online_node always returns MAX_NUMNODES */
struct pglist_data *first_online_pgdat(void)
{
	return NODE_DATA(0);
}

struct pglist_data *next_online_pgdat(struct pglist_data *pgdat)
{
	return NULL; /* Always NULL for single node - next_online_node always returns MAX_NUMNODES */
}

struct zone *next_zone(struct zone *zone)
{
	pg_data_t *pgdat = zone->zone_pgdat;
	/* Simplified for single node: next_online_pgdat always returns NULL */
	if (zone < pgdat->node_zones + MAX_NR_ZONES - 1)
		return zone + 1;
	return NULL;
}

/* zref_in_nodemask always returns 1 - single node kernel */

struct zoneref *__next_zones_zonelist(struct zoneref *z,
				      enum zone_type highest_zoneidx,
				      nodemask_t *nodes)
{
	/* zref_in_nodemask always 1, so !zref_in_nodemask is always false */
	/* Both branches do the same thing now */
	while (zonelist_zone_idx(z) > highest_zoneidx)
		z++;

	return z;
}

void lruvec_init(struct lruvec *lruvec)
{
	enum lru_list lru;

	memset(lruvec, 0, sizeof(struct lruvec));
	spin_lock_init(&lruvec->lru_lock);

	for_each_lru(lru)
		INIT_LIST_HEAD(&lruvec->lists[lru]);

	list_del(&lruvec->lists[LRU_UNEVICTABLE]);
}
