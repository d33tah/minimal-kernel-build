#ifndef WRITEBACK_H
#define WRITEBACK_H

#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/flex_proportions.h>
#include <linux/backing-dev-defs.h>
#include <linux/blk_types.h>

struct bio;

DECLARE_PER_CPU(int, dirty_throttle_leaks);


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
void inode_wait_for_writeback(struct inode *inode);
void inode_io_list_del(struct inode *inode);


static inline void inode_detach_wb(struct inode *inode)
{
}

static inline void wbc_attach_fdatawrite_inode(struct writeback_control *wbc,
					       struct inode *inode)
{
}

static inline void wbc_detach_inode(struct writeback_control *wbc)
{
}

static inline void cgroup_writeback_umount(void)
{
}


void balance_dirty_pages_ratelimited(struct address_space *mapping);

typedef int (*writepage_t)(struct page *page, struct writeback_control *wbc,
				void *data);

int do_writepages(struct address_space *mapping, struct writeback_control *wbc);





#endif		 
