#ifndef WRITEBACK_H
#define WRITEBACK_H

#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/flex_proportions.h>
#include <linux/backing-dev-defs.h>
#include <linux/blk_types.h>

struct bio;

struct backing_dev_info;

struct writeback_control;


static inline void inode_detach_wb(struct inode *inode)
{
}

#endif
