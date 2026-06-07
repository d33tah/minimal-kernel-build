#ifndef __LINUX_MEMORY_HOTPLUG_H
#define __LINUX_MEMORY_HOTPLUG_H

#include <linux/mmzone.h>
#include <linux/notifier.h>

struct vmem_altmap;

/* Unused forward declarations and functions removed:
   arch_alloc_nodedata, generic_alloc_nodedata, arch_refresh_nodedata */

#define pfn_to_online_page(pfn)			\
({						\
	struct page *___page = NULL;		\
	if (pfn_valid(pfn))			\
		___page = pfn_to_page(pfn);	\
	___page;				\
 })

static inline void zone_seqlock_init(struct zone *zone) {}
static inline bool movable_node_is_enabled(void) { return false; }
static inline void pgdat_resize_init(struct pglist_data *pgdat) {}

#endif
