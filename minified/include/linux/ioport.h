
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

/* IORESOURCE_TYPE_BITS removed - unused */
#define IORESOURCE_IO		0x00000100
#define IORESOURCE_MEM		0x00000200

/* IORESOURCE_MUXED removed - unused */
/* IORESOURCE_EXT_TYPE_BITS removed - unused */
#define IORESOURCE_SYSRAM	0x01000000

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

extern struct resource ioport_resource;
extern struct resource iomem_resource;

/* request_resource_conflict inlined into request_resource */
extern int request_resource(struct resource *root, struct resource *new);
/* release_resource, insert_resource_conflict removed - inlined/never called */
extern int insert_resource(struct resource *parent, struct resource *new);
/* resource_type, resource_ext_type removed - never called */



/* request_region, request_muxed_region, request_mem_region, __request_region removed - never used */

/* release_region, release_mem_region, __release_region removed - never called */
/* struct device forward decl removed - unused */
/* iomem_map_sanity_check removed - was stub returning 0 */

extern int
walk_mem_res(u64 start, u64 end, void *arg,
	     int (*func)(struct resource *, void *));

#endif
#endif	 
