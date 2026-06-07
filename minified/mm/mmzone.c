
#include <linux/stddef.h>
#include <linux/mm.h>
#include <linux/mmzone.h>

struct zoneref *__next_zones_zonelist(struct zoneref *z,
				      enum zone_type highest_zoneidx,
				      nodemask_t *nodes)
{
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
