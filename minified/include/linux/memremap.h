#ifndef _LINUX_MEMREMAP_H_
#define _LINUX_MEMREMAP_H_

#include <linux/mm.h>
#include <linux/range.h>
#include <linux/ioport.h>
#include <linux/percpu-refcount.h>

struct resource;
struct device;

struct vmem_altmap {
	unsigned long base_pfn;
	const unsigned long end_pfn;
	const unsigned long reserve;
	unsigned long free;
	unsigned long align;
	unsigned long alloc;
};

enum memory_type {
	MEMORY_DEVICE_PRIVATE = 1,
};

struct dev_pagemap_ops {
	 
	void (*page_free)(struct page *page);

	 
	vm_fault_t (*migrate_to_ram)(struct vm_fault *vmf);
};

#define PGMAP_ALTMAP_VALID	(1 << 0)

struct dev_pagemap {
	struct vmem_altmap altmap;
	struct percpu_ref ref;
	struct completion done;
	enum memory_type type;
	unsigned int flags;
	unsigned long vmemmap_shift;
	const struct dev_pagemap_ops *ops;
	void *owner;
	int nr_range;
	union {
		struct range range;
		struct range ranges[0];
	};
};


static inline struct dev_pagemap *get_dev_pagemap(unsigned long pfn,
		struct dev_pagemap *pgmap)
{
	return NULL;
}


static inline void put_dev_pagemap(struct dev_pagemap *pgmap)
{
	if (pgmap)
		percpu_ref_put(&pgmap->ref);
}

#endif  
