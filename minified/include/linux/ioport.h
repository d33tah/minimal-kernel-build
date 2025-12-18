
#ifndef _LINUX_IOPORT_H
#define _LINUX_IOPORT_H

#ifndef __ASSEMBLY__
#include <linux/bits.h>
#include <linux/compiler.h>
#include <linux/minmax.h>
#include <linux/types.h>
struct resource {
	resource_size_t start;
	resource_size_t end;
	const char *name;
	unsigned long flags;
	unsigned long desc;
	struct resource *parent, *sibling, *child;
};

#define IORESOURCE_BITS		0x000000ff	 

#define IORESOURCE_TYPE_BITS	0x00001f00	 
#define IORESOURCE_IO		0x00000100	 
#define IORESOURCE_MEM		0x00000200
#define IORESOURCE_REG		0x00000300	 
#define IORESOURCE_IRQ		0x00000400

#define IORESOURCE_MEM_64	0x00100000
#define IORESOURCE_MUXED	0x00400000 

#define IORESOURCE_EXT_TYPE_BITS 0x01000000	 
#define IORESOURCE_SYSRAM	0x01000000	 


#define IORESOURCE_EXCLUSIVE	0x08000000	 

#define IORESOURCE_DISABLED	0x10000000
#define IORESOURCE_UNSET	0x20000000	 
#define IORESOURCE_AUTO		0x40000000
#define IORESOURCE_BUSY		0x80000000	 

#define IORESOURCE_SYSTEM_RAM		(IORESOURCE_MEM|IORESOURCE_SYSRAM)


enum {
	IORES_DESC_NONE				= 0,
	IORES_DESC_CRASH_KERNEL			= 1,
	IORES_DESC_ACPI_TABLES			= 2,
	IORES_DESC_ACPI_NV_STORAGE		= 3,
	IORES_DESC_PERSISTENT_MEMORY		= 4,
	IORES_DESC_PERSISTENT_MEMORY_LEGACY	= 5,
	IORES_DESC_DEVICE_PRIVATE_MEMORY	= 6,
	IORES_DESC_RESERVED			= 7,
	IORES_DESC_SOFT_RESERVED		= 8,
};

enum {
	IORES_MAP_SYSTEM_RAM		= BIT(0),
	IORES_MAP_ENCRYPTED		= BIT(1),
};

#define DEFINE_RES_NAMED(_start, _size, _name, _flags)			\
	{								\
		.start = (_start),					\
		.end = (_start) + (_size) - 1,				\
		.name = (_name),					\
		.flags = (_flags),					\
		.desc = IORES_DESC_NONE,				\
	}


#define DEFINE_RES_MEM_NAMED(_start, _size, _name)			\
	DEFINE_RES_NAMED((_start), (_size), (_name), IORESOURCE_MEM)
#define DEFINE_RES_MEM(_start, _size)					\
	DEFINE_RES_MEM_NAMED((_start), (_size), NULL)


extern struct resource ioport_resource;
extern struct resource iomem_resource;

extern struct resource *request_resource_conflict(struct resource *root, struct resource *new);
extern int request_resource(struct resource *root, struct resource *new);
extern int release_resource(struct resource *new);
extern struct resource *insert_resource_conflict(struct resource *parent, struct resource *new);
extern int insert_resource(struct resource *parent, struct resource *new);
/* allocate_resource, lookup_resource, adjust_resource removed - unused */
static inline resource_size_t resource_size(const struct resource *res)
{
	return res->end - res->start + 1;
}
static inline unsigned long resource_type(const struct resource *res)
{
	return res->flags & IORESOURCE_TYPE_BITS;
}
static inline unsigned long resource_ext_type(const struct resource *res)
{
	return res->flags & IORESOURCE_EXT_TYPE_BITS;
}
/* resource_contains removed - unused */

static inline bool resource_overlaps(struct resource *r1, struct resource *r2)
{
       return r1->start <= r2->end && r1->end >= r2->start;
}


#define request_region(start,n,name)		__request_region(&ioport_resource, (start), (n), (name), 0)
#define request_muxed_region(start,n,name)	__request_region(&ioport_resource, (start), (n), (name), IORESOURCE_MUXED)
#define __request_mem_region(start,n,name, excl) __request_region(&iomem_resource, (start), (n), (name), excl)
#define request_mem_region(start,n,name) __request_region(&iomem_resource, (start), (n), (name), 0)
#define request_mem_region_muxed(start, n, name) \
	__request_region(&iomem_resource, (start), (n), (name), IORESOURCE_MUXED)
#define request_mem_region_exclusive(start,n,name) \
	__request_region(&iomem_resource, (start), (n), (name), IORESOURCE_EXCLUSIVE)
#define rename_region(region, newname) do { (region)->name = (newname); } while (0)

extern struct resource * __request_region(struct resource *,
					resource_size_t start,
					resource_size_t n,
					const char *name, int flags);

#define release_region(start,n)	__release_region(&ioport_resource, (start), (n))
#define release_mem_region(start,n)	__release_region(&iomem_resource, (start), (n))

extern void __release_region(struct resource *, resource_size_t,
				resource_size_t);

struct device;


#define devm_request_region(dev,start,n,name) \
	__devm_request_region(dev, &ioport_resource, (start), (n), (name))
#define devm_request_mem_region(dev,start,n,name) \
	__devm_request_region(dev, &iomem_resource, (start), (n), (name))

extern struct resource * __devm_request_region(struct device *dev,
				struct resource *parent, resource_size_t start,
				resource_size_t n, const char *name);

#define devm_release_region(dev, start, n) \
	__devm_release_region(dev, &ioport_resource, (start), (n))
#define devm_release_mem_region(dev, start, n) \
	__devm_release_region(dev, &iomem_resource, (start), (n))

extern void __devm_release_region(struct device *dev, struct resource *parent,
				  resource_size_t start, resource_size_t n);
extern int iomem_map_sanity_check(resource_size_t addr, unsigned long size);

extern int
walk_mem_res(u64 start, u64 end, void *arg,
	     int (*func)(struct resource *, void *));

struct resource *devm_request_free_mem_region(struct device *dev,
		struct resource *base, unsigned long size);
struct resource *request_free_mem_region(struct resource *base,
		unsigned long size, const char *name);


#endif
#endif	 
