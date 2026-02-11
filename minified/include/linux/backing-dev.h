#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H
/* linux/kernel.h removed - no kernel.h macros used */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/list.h>
/* linux/radix-tree.h removed - no radix tree used */
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/percpu_counter.h>
#include <linux/percpu-refcount.h>
/* flex_proportions.h inlined */
struct fprop_local_percpu {
	struct percpu_counter events;
	raw_spinlock_t lock;
};
void fprop_local_init_percpu(struct fprop_local_percpu *pl, gfp_t gfp);
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/kref.h>
#include <linux/refcount.h>

/* struct page, dentry forward decls removed - unused */
struct device;

enum wb_state {
	WB_registered,
	/* WB_writeback_running, WB_has_dirty_io removed - unused */
};

/* enum wb_stat_item removed - NR_WB_STAT_ITEMS was 0, stat array unused */
/* enum wb_reason removed - only used in unused field start_all_reason */

struct bdi_writeback {
	/* bdi removed - write-only, never read */

	unsigned long state;
	/* last_old_flush removed - write-only field */

	struct list_head b_dirty;
	struct list_head b_io;
	struct list_head b_more_io;
	struct list_head b_dirty_time;
	spinlock_t list_lock;

	atomic_t writeback_inodes;
	/* stat[NR_WB_STAT_ITEMS], congested removed - unused */

	/* bw_time_stamp, dirtied_stamp, written_stamp, write_bandwidth,
	   avg_write_bandwidth, dirty_ratelimit, balanced_dirty_ratelimit,
	   dirty_exceeded, dirty_sleep removed - write-only fields */

	struct fprop_local_percpu completions;

	spinlock_t work_lock;
	struct list_head work_list;
	struct delayed_work dwork;
	/* bw_dwork removed - never scheduled, only initialized */

	struct list_head bdi_node;

};

struct backing_dev_info {
	u64 id;
	/* rb_node removed - bdi_tree was never inserted into */
	struct list_head bdi_list;
	unsigned long ra_pages;
	unsigned long io_pages;

	struct kref refcnt;
	unsigned int capabilities;
	/* min_ratio, max_ratio, max_prop_frac removed - write-only */

	/* tot_write_bandwidth removed - never used */

	struct bdi_writeback wb;
	struct list_head wb_list;
	wait_queue_head_t wb_waitq;

	struct device *dev;
	char dev_name[64];
	struct device *owner;

	/* laptop_mode_wb_timer removed - never set up, only deleted */

};
/* bdi_put, bdi_unregister removed - no callers */
void wb_workfn(struct work_struct *work);
/* bdi_set_min_ratio, bdi_set_max_ratio removed - never called */
#define BDI_CAP_WRITEBACK		(1 << 0)
/* BDI_CAP_WRITEBACK_ACCT removed - unused */
extern struct backing_dev_info noop_backing_dev_info;
int bdi_init(struct backing_dev_info *bdi);
struct backing_dev_info *inode_to_bdi(struct inode *inode);
static inline bool mapping_can_writeback(struct address_space *mapping) { return inode_to_bdi(mapping->host)->capabilities & BDI_CAP_WRITEBACK; }
/* inode_to_wb removed - unused */
#endif
