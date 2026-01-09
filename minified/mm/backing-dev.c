
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

struct backing_dev_info noop_backing_dev_info;
/* bdi_class removed - never used */

DEFINE_SPINLOCK(bdi_lock);
static struct rb_root bdi_tree = RB_ROOT;
LIST_HEAD(bdi_list);

struct workqueue_struct *bdi_wq;

#define K(x) ((x) << (PAGE_SHIFT - 10))

/* bdi_debug_init, bdi_debug_unregister, bdi sysfs attrs, bdi_class_init removed - unused */

/* default_bdi_init removed - alloc_workqueue hangs with low memory */

static void wb_update_bandwidth_workfn(struct work_struct *work)
{
	/* Stub: bw_dwork never scheduled in minimal kernel */
}

#define INIT_BW (100 << (20 - PAGE_SHIFT))

static int wb_init(struct bdi_writeback *wb, struct backing_dev_info *bdi,
		   gfp_t gfp)
{
	int err;
	/* i removed - stat init loop removed */

	memset(wb, 0, sizeof(*wb));

	wb->bdi = bdi;
	wb->last_old_flush = 0; /* Simplified for minimal kernel */
	INIT_LIST_HEAD(&wb->b_dirty);
	INIT_LIST_HEAD(&wb->b_io);
	INIT_LIST_HEAD(&wb->b_more_io);
	INIT_LIST_HEAD(&wb->b_dirty_time);
	spin_lock_init(&wb->list_lock);

	atomic_set(&wb->writeback_inodes, 0);
	wb->bw_time_stamp = 0; /* Simplified for minimal kernel */
	wb->balanced_dirty_ratelimit = INIT_BW;
	wb->dirty_ratelimit = INIT_BW;
	wb->write_bandwidth = INIT_BW;
	wb->avg_write_bandwidth = INIT_BW;

	spin_lock_init(&wb->work_lock);
	INIT_LIST_HEAD(&wb->work_list);
	INIT_DELAYED_WORK(&wb->dwork, wb_workfn);
	INIT_DELAYED_WORK(&wb->bw_dwork, wb_update_bandwidth_workfn);
	wb->dirty_sleep = 0; /* Simplified for minimal kernel */

	err = fprop_local_init_percpu(&wb->completions, gfp);
	if (err)
		return err;

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

static void wb_exit(struct bdi_writeback *wb)
{
	WARN_ON(delayed_work_pending(&wb->dwork));
	/* stat destroy loop removed - NR_WB_STAT_ITEMS was 0 */
	fprop_local_destroy_percpu(&wb->completions);
}

/* Removed: cgwb_bdi_unregister - empty stub (~3 LOC) */

static void cgwb_remove_from_bdi_list(struct bdi_writeback *wb)
{
	list_del_rcu(&wb->bdi_node);
}

int bdi_init(struct backing_dev_info *bdi)
{
	bdi->dev = NULL;

	kref_init(&bdi->refcnt);
	/* bdi->min_ratio removed - always 0 */
	bdi->max_ratio = 100;
	bdi->max_prop_frac = FPROP_FRAC_BASE;
	INIT_LIST_HEAD(&bdi->bdi_list);
	INIT_LIST_HEAD(&bdi->wb_list);
	init_waitqueue_head(&bdi->wb_waitq);

	return wb_init(&bdi->wb, bdi, GFP_KERNEL);
}

static void bdi_remove_from_list(struct backing_dev_info *bdi)
{
	spin_lock_bh(&bdi_lock);
	rb_erase(&bdi->rb_node, &bdi_tree);
	list_del_rcu(&bdi->bdi_list);
	spin_unlock_bh(&bdi_lock);

	synchronize_rcu_expedited();
}

void bdi_unregister(struct backing_dev_info *bdi)
{
	del_timer_sync(&bdi->laptop_mode_wb_timer);

	bdi_remove_from_list(bdi);
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
	wb_exit(&bdi->wb);
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
