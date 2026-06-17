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

enum writeback_sync_modes {
	WB_SYNC_NONE,	 
	WB_SYNC_ALL,	 
};

struct writeback_control {
	long nr_to_write;		 
	long pages_skipped;		 

	 
	loff_t range_start;
	loff_t range_end;

	enum writeback_sync_modes sync_mode;

	unsigned for_kupdate:1;		 
	unsigned for_background:1;	 
	unsigned tagged_writepages:1;	 
	unsigned for_reclaim:1;		 
	unsigned range_cyclic:1;	 
	unsigned for_sync:1;		 
	unsigned unpinned_fscache_wb:1;	 

	 
	unsigned no_cgroup_owner:1;

	unsigned punt_to_cgroup:1;	 

	 
	struct swap_iocb **swap_plug;

};

struct wb_domain {
	spinlock_t lock;

	 
	struct fprop_global completions;
	struct timer_list period_timer;	 
	unsigned long period_time;

	 
	unsigned long dirty_limit_tstamp;
	unsigned long dirty_limit;
};


struct bdi_writeback;


static inline void inode_detach_wb(struct inode *inode)
{
}



typedef int (*writepage_t)(struct page *page, struct writeback_control *wbc,
				void *data);






#endif		 
