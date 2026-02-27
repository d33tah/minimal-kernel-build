
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
	struct resource *parent, *sibling, *child;
};

#define IORESOURCE_IO		0x00000100
#define IORESOURCE_MEM		0x00000200

#define IORESOURCE_SYSRAM	0x01000000

#define IORESOURCE_BUSY		0x80000000

#define IORESOURCE_SYSTEM_RAM		(IORESOURCE_MEM|IORESOURCE_SYSRAM)

extern struct resource ioport_resource;
extern struct resource iomem_resource;

extern int insert_resource(struct resource *parent, struct resource *new);

#endif
#endif	 
