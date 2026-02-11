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

static inline bool __ref_is_percpu(struct percpu_ref *ref,
				   unsigned long __percpu **percpu_countp)
{
	unsigned long percpu_ptr;

	percpu_ptr = READ_ONCE(ref->percpu_count_ptr);

	if (unlikely(percpu_ptr & __PERCPU_REF_ATOMIC_DEAD))
		return false;

	*percpu_countp = (unsigned long __percpu *)percpu_ptr;
	return true;
}

static inline void percpu_ref_put_many(struct percpu_ref *ref,
				       unsigned long nr)
{
	unsigned long __percpu *percpu_count;

	rcu_read_lock();

	if (__ref_is_percpu(ref, &percpu_count))
		this_cpu_sub(*percpu_count, nr);
	else if (unlikely(atomic_long_sub_and_test(nr, &ref->data->count)))
		ref->data->release(ref);

	rcu_read_unlock();
}

static inline void percpu_ref_put(struct percpu_ref *ref)
{
	percpu_ref_put_many(ref, 1);
}
/* end percpu-refcount.h */
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
