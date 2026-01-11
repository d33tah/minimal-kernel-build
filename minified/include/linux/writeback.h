#ifndef WRITEBACK_H
#define WRITEBACK_H

#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/flex_proportions.h>
#include <linux/backing-dev-defs.h>
#include <linux/blk_types.h>

/* struct bio forward decl removed - already in blk_types.h */

/* dirty_throttle_leaks DECLARE removed - counter never read */

struct backing_dev_info;

enum writeback_sync_modes {
	WB_SYNC_ALL,	/* WB_SYNC_NONE removed - unused */
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

	/* struct swap_iocb **swap_plug removed - never used */
};

/* wb_domain struct removed - never used */
/* writeback_inodes_sb, try_to_writeback_inodes_sb, sync_inodes_sb removed - never called */
/* wakeup_flusher_threads, inode_wait_for_writeback, inode_io_list_del removed - never called */

/* wb_update_bandwidth, do_writepages removed - never called */

#endif		 
