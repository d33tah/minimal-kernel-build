
#include <linux/mmzone.h>

/* next_online_pgdat removed - only used by the unused for_each_online_pgdat macro */

struct zoneref *__next_zones_zonelist(struct zoneref *z,
				      enum zone_type highest_zoneidx)
{
	while (zonelist_zone_idx(z) > highest_zoneidx)
		z++;

	return z;
}
