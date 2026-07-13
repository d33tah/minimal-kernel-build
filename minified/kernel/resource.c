
#include <linux/ioport.h>


struct resource iomem_resource = { .name	= "PCI mem", .start	= 0, .end	= -1, .flags	= IORESOURCE_MEM, };

/*
 * insert_resource()/__insert_resource() removed: their sole caller
 * (e820__reserve_resources) is gone, and the iomem_resource tree they wrote
 * into is never walked on this build.
 *
 * ioport_resource + request_resource()/__request_resource() + resource_lock
 * removed: their only callers were the 6 vgacon.c request_resource() console
 * I/O-port reservations, which were write-only into the never-walked ioport
 * tree (return value ignored).
 */
