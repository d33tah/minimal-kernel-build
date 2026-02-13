#ifndef _LINUX_BACKING_DEV_H
#define _LINUX_BACKING_DEV_H
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/percpu_counter.h>
/* percpu-refcount.h inlined */
#include <linux/atomic.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/types.h>
#include <linux/gfp.h>

struct percpu_ref;
typedef void(percpu_ref_func_t)(struct percpu_ref *);

enum {
	__PERCPU_REF_ATOMIC = 1LU << 0,
	__PERCPU_REF_DEAD = 1LU << 1,
	__PERCPU_REF_ATOMIC_DEAD = __PERCPU_REF_ATOMIC | __PERCPU_REF_DEAD,

	__PERCPU_REF_FLAG_BITS = 2,
};

struct percpu_ref_data {
	atomic_long_t count;
	percpu_ref_func_t *release;
	percpu_ref_func_t *confirm_switch;
	bool force_atomic : 1;
	bool allow_reinit : 1;
	struct rcu_head rcu;
	struct percpu_ref *ref;
};

struct percpu_ref {
	unsigned long percpu_count_ptr;
	struct percpu_ref_data *data;
};

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

struct device;

enum wb_state {
	WB_registered,
};

struct bdi_writeback {

	unsigned long state;

	struct list_head b_dirty;
	struct list_head b_io;
	struct list_head b_more_io;
	struct list_head b_dirty_time;
	spinlock_t list_lock;

	atomic_t writeback_inodes;

	/* bw_time_stamp, dirtied_stamp, written_stamp, write_bandwidth,
	   avg_write_bandwidth, dirty_ratelimit, balanced_dirty_ratelimit,
	   dirty_exceeded, dirty_sleep removed - write-only fields */

	struct fprop_local_percpu completions;

	spinlock_t work_lock;
	struct list_head work_list;
	struct delayed_work dwork;

	struct list_head bdi_node;

};

struct backing_dev_info {
	u64 id;
	struct list_head bdi_list;
	unsigned long ra_pages;
	unsigned long io_pages;

	struct kref refcnt;
	unsigned int capabilities;

	struct bdi_writeback wb;
	struct list_head wb_list;
	wait_queue_head_t wb_waitq;

	struct device *dev;
	char dev_name[64];
	struct device *owner;

};
void wb_workfn(struct work_struct *work);
#define BDI_CAP_WRITEBACK		(1 << 0)
extern struct backing_dev_info noop_backing_dev_info;
int bdi_init(struct backing_dev_info *bdi);
struct backing_dev_info *inode_to_bdi(struct inode *inode);
static inline bool mapping_can_writeback(struct address_space *mapping) { return inode_to_bdi(mapping->host)->capabilities & BDI_CAP_WRITEBACK; }
#endif
