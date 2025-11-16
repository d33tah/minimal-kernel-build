 
#ifndef __LINUX_MEMORY_HOTPLUG_H
#define __LINUX_MEMORY_HOTPLUG_H

#include <linux/mmzone.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/bug.h>

struct page;
struct zone;
struct pglist_data;
struct mem_section;
struct memory_block;
struct memory_group;
struct resource;
struct vmem_altmap;
struct dev_pagemap;


#define arch_alloc_nodedata(nid)	generic_alloc_nodedata(nid)


 
static inline pg_data_t *generic_alloc_nodedata(int nid)
{
	BUG();
	return NULL;
}
static inline void arch_refresh_nodedata(int nid, pg_data_t *pgdat)
{
}

#define pfn_to_online_page(pfn)			\
({						\
	struct page *___page = NULL;		\
	if (pfn_valid(pfn))			\
		___page = pfn_to_page(pfn);	\
	___page;				\
 })

static inline void zone_seqlock_init(struct zone *zone) {}

static inline bool movable_node_is_enabled(void)
{
	return false;
}


struct range arch_get_mappable_range(void);


static inline void pgdat_resize_init(struct pglist_data *pgdat) {}

extern void set_zone_contiguous(struct zone *zone);
extern void clear_zone_contiguous(struct zone *zone);

#endif  
