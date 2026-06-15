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

struct bdi_writeback {
	struct backing_dev_info *bdi;
};

struct backing_dev_info {
	u64 id;
	struct rb_node rb_node;  
	struct list_head bdi_list;
	unsigned long ra_pages;	 
	unsigned long io_pages;	 

	struct kref refcnt;	 
	unsigned int capabilities;  
	unsigned int min_ratio;
	unsigned int max_ratio, max_prop_frac;

	 
	atomic_long_t tot_write_bandwidth;

	struct bdi_writeback wb;   
	struct list_head wb_list;  
	wait_queue_head_t wb_waitq;

	struct device *dev;
	char dev_name[64];
	struct device *owner;

	struct timer_list laptop_mode_wb_timer;

};


#endif
