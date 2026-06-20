
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/pfn.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/acpi.h>
#include <uapi/linux/magic.h>
#include <asm/io.h>


struct resource ioport_resource = {
	.name	= "PCI IO",
	.start	= 0,
	.end	= IO_SPACE_LIMIT,
	.flags	= IORESOURCE_IO,
};

struct resource iomem_resource = {
	.name	= "PCI mem",
	.start	= 0,
	.end	= -1,
	.flags	= IORESOURCE_MEM,
};

/*
 * insert_resource()/__insert_resource() removed: their sole caller
 * (e820__reserve_resources) is gone, and the iomem_resource tree they wrote
 * into is never walked on this build.
 *
 * request_resource()/__request_resource() + resource_lock removed: their only
 * callers were the 6 vgacon.c request_resource(&ioport_resource, ...) console
 * I/O-port reservations, which were write-only into the never-walked ioport
 * tree (return value ignored).
 */
