
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
};

#define IORESOURCE_MEM		0x00000200

extern struct resource iomem_resource;



struct device;


#endif
#endif	 
