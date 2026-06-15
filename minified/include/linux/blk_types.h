#ifndef __LINUX_BLK_TYPES_H
#define __LINUX_BLK_TYPES_H

#include <linux/types.h>
#include <linux/bvec.h>
#include <linux/device.h>

struct bio;
struct page;

/* Minimal block_device struct - only pointers used, never instantiated */
struct block_device {
	int dummy;
};

#endif
