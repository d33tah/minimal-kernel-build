

#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/mmzone.h>

struct pglist_data *first_online_pgdat(void)
{
	return NODE_DATA(first_online_node);
}

/* next_online_pgdat removed - only used by the unused for_each_online_pgdat macro */

struct zone *next_zone(struct zone *zone)
{
	pg_data_t *pgdat = zone->zone_pgdat;

	if (zone < pgdat->node_zones + MAX_NR_ZONES - 1)
		zone++;
	else
		zone = NULL;
	return zone;
}

struct zoneref *__next_zones_zonelist(struct zoneref *z,
					enum zone_type highest_zoneidx,
					nodemask_t *nodes)
{

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

