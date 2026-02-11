
/* linux/blkdev.h removed - empty stub */
#include <linux/wait.h>
#include <linux/backing-dev.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched.h>
#include <linux/device.h>
/* flex_proportions.h - inlined into backing-dev.h */

/* Merged from lib/flex_proportions.c */
void fprop_local_init_percpu(struct fprop_local_percpu *pl, gfp_t gfp)
{
	percpu_counter_init(&pl->events, 0, gfp);
	raw_spin_lock_init(&pl->lock);
}

struct backing_dev_info noop_backing_dev_info;

/* Stub for wb dwork - never actually runs */
void wb_workfn(struct work_struct *work)
{
}

int bdi_init(struct backing_dev_info *bdi)
{
	struct bdi_writeback *wb = &bdi->wb;

	bdi->dev = NULL;
	kref_init(&bdi->refcnt);
	INIT_LIST_HEAD(&bdi->bdi_list);
	INIT_LIST_HEAD(&bdi->wb_list);
	init_waitqueue_head(&bdi->wb_waitq);

	memset(wb, 0, sizeof(*wb));
	INIT_LIST_HEAD(&wb->b_dirty);
	INIT_LIST_HEAD(&wb->b_io);
	INIT_LIST_HEAD(&wb->b_more_io);
	INIT_LIST_HEAD(&wb->b_dirty_time);
	spin_lock_init(&wb->list_lock);
	atomic_set(&wb->writeback_inodes, 0);
	spin_lock_init(&wb->work_lock);
	INIT_LIST_HEAD(&wb->work_list);
	INIT_DELAYED_WORK(&wb->dwork, wb_workfn);
	fprop_local_init_percpu(&wb->completions, GFP_KERNEL);

	return 0;
}

/* bdi_unregister, bdi_put removed - no callers */

struct backing_dev_info *inode_to_bdi(struct inode *inode)
{
	struct super_block *sb;

	if (!inode)
		return &noop_backing_dev_info;

	sb = inode->i_sb;
	return sb->s_bdi;
}
