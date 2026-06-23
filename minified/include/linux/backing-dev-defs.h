#ifndef __LINUX_BACKING_DEV_DEFS_H
#define __LINUX_BACKING_DEV_DEFS_H

#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/kref.h>
#include <linux/refcount.h>

struct page;
struct device;
struct dentry;

struct backing_dev_info {
};


#endif
