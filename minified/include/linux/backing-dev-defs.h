#ifndef __LINUX_BACKING_DEV_DEFS_H
#define __LINUX_BACKING_DEV_DEFS_H

#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/percpu_counter.h>
#include <linux/percpu-refcount.h>
#include <linux/flex_proportions.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/kref.h>
#include <linux/refcount.h>

struct page;
struct device;
struct dentry;

enum wb_state {
	WB_registered,
	/* WB_writeback_running, WB_has_dirty_io removed - unused */
};

/* enum wb_stat_item removed - NR_WB_STAT_ITEMS was 0, stat array unused */
/* enum wb_reason removed - only used in unused field start_all_reason */

struct bdi_writeback {
	struct backing_dev_info *bdi;	 

	unsigned long state;		 
	unsigned long last_old_flush;	 

	struct list_head b_dirty;	 
	struct list_head b_io;		 
	struct list_head b_more_io;	 
	struct list_head b_dirty_time;	 
	spinlock_t list_lock;		 

	atomic_t writeback_inodes;
	/* stat[NR_WB_STAT_ITEMS] removed - array was zero-sized */

	unsigned long congested;	 

	unsigned long bw_time_stamp;	 
	unsigned long dirtied_stamp;
	unsigned long written_stamp;	 
	unsigned long write_bandwidth;	 
	unsigned long avg_write_bandwidth;  

	 
	unsigned long dirty_ratelimit;
	unsigned long balanced_dirty_ratelimit;

	struct fprop_local_percpu completions;
	int dirty_exceeded;
	/* start_all_reason field removed - never used */

	spinlock_t work_lock;
	struct list_head work_list;
	struct delayed_work dwork;	 
	struct delayed_work bw_dwork;	 

	unsigned long dirty_sleep;	 

	struct list_head bdi_node;	 

};

struct backing_dev_info {
	u64 id;
	struct rb_node rb_node;  
	struct list_head bdi_list;
	unsigned long ra_pages;	 
	unsigned long io_pages;	 

	struct kref refcnt;
	unsigned int capabilities;
	/* min_ratio removed - always 0 */
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
