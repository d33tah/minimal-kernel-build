// SPDX-License-Identifier: GPL-2.0-only
/* Stubbed resource.c */
#include <linux/export.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/resource_ext.h>
#include <asm/io.h>

struct resource ioport_resource = {
	.name	= "PCI IO",
	.start	= 0,
	.end	= IO_SPACE_LIMIT,
	.flags	= IORESOURCE_IO,
};
EXPORT_SYMBOL(ioport_resource);

struct resource iomem_resource = {
	.name	= "PCI mem",
	.start	= 0,
	.end	= -1,
	.flags	= IORESOURCE_MEM,
};
EXPORT_SYMBOL(iomem_resource);

int request_resource(struct resource *root, struct resource *new) { return 0; }
EXPORT_SYMBOL(request_resource);

int release_resource(struct resource *old) { return 0; }
EXPORT_SYMBOL(release_resource);

int walk_iomem_res_desc(unsigned long desc, unsigned long flags, u64 start,
			u64 end, void *arg,
			int (*func)(struct resource *, void *)) { return 0; }
EXPORT_SYMBOL_GPL(walk_iomem_res_desc);

int page_is_ram(unsigned long pfn) { return 0; }
EXPORT_SYMBOL_GPL(page_is_ram);

int region_intersects(resource_size_t start, size_t size, unsigned long flags,
		      unsigned long desc) { return 0; }
EXPORT_SYMBOL_GPL(region_intersects);

int allocate_resource(struct resource *root, struct resource *new,
		      resource_size_t size, resource_size_t min,
		      resource_size_t max, resource_size_t align,
		      resource_size_t (*alignf)(void *, const struct resource *,
						resource_size_t, resource_size_t),
		      void *alignf_data) { return -ENOMEM; }
EXPORT_SYMBOL(allocate_resource);

int insert_resource(struct resource *parent, struct resource *new) { return 0; }
EXPORT_SYMBOL_GPL(insert_resource);

int remove_resource(struct resource *old) { return 0; }
EXPORT_SYMBOL_GPL(remove_resource);

int adjust_resource(struct resource *res, resource_size_t start,
		    resource_size_t size) { return 0; }
EXPORT_SYMBOL(adjust_resource);

struct resource *__request_region(struct resource *parent,
				  resource_size_t start, resource_size_t n,
				  const char *name, int flags) { return NULL; }
EXPORT_SYMBOL(__request_region);

void __release_region(struct resource *parent, resource_size_t start,
		      resource_size_t n) { }
EXPORT_SYMBOL(__release_region);

int devm_request_resource(struct device *dev, struct resource *root,
			  struct resource *new) { return 0; }
EXPORT_SYMBOL(devm_request_resource);

void devm_release_resource(struct device *dev, struct resource *new) { }
EXPORT_SYMBOL(devm_release_resource);

struct resource *__devm_request_region(struct device *dev,
				       struct resource *parent,
				       resource_size_t start,
				       resource_size_t n, const char *name) { return NULL; }
EXPORT_SYMBOL(__devm_request_region);

void __devm_release_region(struct device *dev, struct resource *parent,
			   resource_size_t start, resource_size_t n) { }
EXPORT_SYMBOL(__devm_release_region);

struct resource_entry *resource_list_create_entry(struct resource *res,
					    size_t extra_size) { return NULL; }
EXPORT_SYMBOL(resource_list_create_entry);

void resource_list_free(struct list_head *head) { }
EXPORT_SYMBOL(resource_list_free);

void __init early_init_dt_add_memory_arch(u64 base, u64 size) { }

int walk_system_ram_range(unsigned long start_pfn, unsigned long nr_pages,
			  void *arg, int (*func)(unsigned long, unsigned long, void *)) { return 0; }

int walk_mem_res(u64 start, u64 end, void *arg,
		 int (*func)(struct resource *, void *)) { return 0; }

int iomem_map_sanity_check(resource_size_t addr, unsigned long size) { return 0; }

void insert_resource_expand_to_fit(struct resource *root, struct resource *new) { }

void reserve_region_with_split(struct resource *root, resource_size_t start,
			       resource_size_t end, const char *name) { }
