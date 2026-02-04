

#ifndef _LINUX_BLKDEV_H
#define _LINUX_BLKDEV_H

#include <linux/types.h>
#include <linux/blk_types.h>
#include <linux/device.h>
/* linux/list.h removed - no list structures used */
#include <linux/workqueue.h>
#include <linux/gfp.h>
#include <linux/kdev_t.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>

/* struct module, kiocb, request_queue, gendisk, blk_plug removed - unused */

/* blk_flush_plug removed - no callers */

static inline void printk_all_partitions(void)
{
}

#define BDEVNAME_SIZE	32

#endif
