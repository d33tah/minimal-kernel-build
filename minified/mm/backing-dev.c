
#include <linux/blkdev.h>
#include <linux/wait.h>
#include <linux/rbtree.h>
#include <linux/kthread.h>
#include <linux/backing-dev.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/device.h>
#include <linux/flex_proportions.h>

/* Merged from lib/flex_proportions.c */
/* percpu_counter_init always returns 0 - simplified */
void fprop_local_init_percpu(struct fprop_local_percpu *pl, gfp_t gfp)
{
	percpu_counter_init(&pl->events, 0, gfp);
	pl->period = 0;
	raw_spin_lock_init(&pl->lock);
}
void fprop_local_destroy_percpu(struct fprop_local_percpu *pl)
{
	percpu_counter_destroy(&pl->events);
}

struct backing_dev_info noop_backing_dev_info;
/* bdi_class removed - never used */

DEFINE_SPINLOCK(bdi_lock);
/* bdi_tree removed - never inserted into, only erased from */
LIST_HEAD(bdi_list);

struct workqueue_struct *bdi_wq;

/* K(x) macro removed - unused */
/* bdi_debug_init, bdi_debug_unregister, bdi sysfs attrs, bdi_class_init removed - unused */

/* default_bdi_init removed - alloc_workqueue hangs with low memory */

static void wb_update_bandwidth_workfn(struct work_struct *work)
{
	/* Stub: bw_dwork never scheduled in minimal kernel */
}

/* From fs-writeback.c - stub for wb dwork */
void wb_workfn(struct work_struct *work)
{
}

/* INIT_BW removed - no longer used after field removal */

static int wb_init(struct bdi_writeback *wb, struct backing_dev_info *bdi,
		   gfp_t gfp)
{
	/* i, err removed - stat init loop and error handling removed */

	memset(wb, 0, sizeof(*wb));

	wb->bdi = bdi;
	/* last_old_flush init removed - field removed */
	INIT_LIST_HEAD(&wb->b_dirty);
	INIT_LIST_HEAD(&wb->b_io);
	INIT_LIST_HEAD(&wb->b_more_io);
	INIT_LIST_HEAD(&wb->b_dirty_time);
	spin_lock_init(&wb->list_lock);

	atomic_set(&wb->writeback_inodes, 0);
	/* bw_time_stamp, balanced_dirty_ratelimit, dirty_ratelimit,
	   write_bandwidth, avg_write_bandwidth, dirty_sleep inits removed
	   - fields removed from struct */

	spin_lock_init(&wb->work_lock);
	INIT_LIST_HEAD(&wb->work_list);
	INIT_DELAYED_WORK(&wb->dwork, wb_workfn);
	INIT_DELAYED_WORK(&wb->bw_dwork, wb_update_bandwidth_workfn);

	/* fprop_local_init_percpu now void - always succeeds */
	fprop_local_init_percpu(&wb->completions, gfp);

	/* stat init loop removed - NR_WB_STAT_ITEMS is 0 */

	return 0;
}

static void cgwb_remove_from_bdi_list(struct bdi_writeback *wb);

static void wb_shutdown(struct bdi_writeback *wb)
{
	spin_lock_bh(&wb->work_lock);
	if (!test_and_clear_bit(WB_registered, &wb->state)) {
		spin_unlock_bh(&wb->work_lock);
		return;
	}
	spin_unlock_bh(&wb->work_lock);

	cgwb_remove_from_bdi_list(wb);

	mod_delayed_work(bdi_wq, &wb->dwork, 0);
	flush_delayed_work(&wb->dwork);
	WARN_ON(!list_empty(&wb->work_list));
	flush_delayed_work(&wb->bw_dwork);
}

/* wb_exit inlined into release_bdi */

static void cgwb_remove_from_bdi_list(struct bdi_writeback *wb)
{
	list_del_rcu(&wb->bdi_node);
}

int bdi_init(struct backing_dev_info *bdi)
{
	bdi->dev = NULL;

	kref_init(&bdi->refcnt);
	/* min_ratio, max_ratio, max_prop_frac assignments removed - fields removed */
	INIT_LIST_HEAD(&bdi->bdi_list);
	INIT_LIST_HEAD(&bdi->wb_list);
	init_waitqueue_head(&bdi->wb_waitq);

	return wb_init(&bdi->wb, bdi, GFP_KERNEL);
}

/* bdi_remove_from_list removed - inlined into single caller (~8 LOC) */

void bdi_unregister(struct backing_dev_info *bdi)
{
	/* del_timer_sync laptop_mode_wb_timer removed - field removed */

	/* Inlined bdi_remove_from_list */
	spin_lock_bh(&bdi_lock);
	list_del_rcu(&bdi->bdi_list);
	spin_unlock_bh(&bdi_lock);
	synchronize_rcu_expedited();
	wb_shutdown(&bdi->wb);
	/* bdi_set_min_ratio call removed - min_ratio always 0 */
	if (bdi->dev) {
		device_unregister(bdi->dev);
		bdi->dev = NULL;
	}

	if (bdi->owner) {
		put_device(bdi->owner);
		bdi->owner = NULL;
	}
}

static void release_bdi(struct kref *ref)
{
	struct backing_dev_info *bdi =
		container_of(ref, struct backing_dev_info, refcnt);

	WARN_ON_ONCE(test_bit(WB_registered, &bdi->wb.state));
	WARN_ON_ONCE(bdi->dev);
	WARN_ON(delayed_work_pending(&bdi->wb.dwork));
	fprop_local_destroy_percpu(&bdi->wb.completions);
	kfree(bdi);
}

void bdi_put(struct backing_dev_info *bdi)
{
	kref_put(&bdi->refcnt, release_bdi);
}

struct backing_dev_info *inode_to_bdi(struct inode *inode)
{
	struct super_block *sb;

	if (!inode)
		return &noop_backing_dev_info;

	sb = inode->i_sb;
	return sb->s_bdi;
}
