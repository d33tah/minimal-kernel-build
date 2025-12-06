
#include <linux/blkdev.h>
#include <linux/wait.h>
#include <linux/rbtree.h>
#include <linux/kthread.h>
#include <linux/backing-dev.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/device.h>

struct backing_dev_info noop_backing_dev_info;

static struct class *bdi_class;
static const char *bdi_unknown_name = "(unknown)";

DEFINE_SPINLOCK(bdi_lock);
static u64 bdi_id_cursor;
static struct rb_root bdi_tree = RB_ROOT;
LIST_HEAD(bdi_list);

struct workqueue_struct *bdi_wq;

#define K(x) ((x) << (PAGE_SHIFT - 10))

static inline void bdi_debug_init(void)
{
}
static inline void bdi_debug_register(struct backing_dev_info *bdi,
				      const char *name)
{
}
static inline void bdi_debug_unregister(struct backing_dev_info *bdi)
{
}

/* Stub: BDI sysfs attributes simplified for minimal kernel */
static ssize_t read_ahead_kb_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) { return count; }
static ssize_t read_ahead_kb_show(struct device *dev,
		struct device_attribute *attr, char *buf) { return sysfs_emit(buf, "0\n"); }
static DEVICE_ATTR_RW(read_ahead_kb);

static ssize_t min_ratio_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) { return count; }
static ssize_t min_ratio_show(struct device *dev,
		struct device_attribute *attr, char *buf) { return sysfs_emit(buf, "0\n"); }
static DEVICE_ATTR_RW(min_ratio);

static ssize_t max_ratio_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) { return count; }
static ssize_t max_ratio_show(struct device *dev,
		struct device_attribute *attr, char *buf) { return sysfs_emit(buf, "0\n"); }
static DEVICE_ATTR_RW(max_ratio);

static ssize_t stable_pages_required_show(struct device *dev,
		struct device_attribute *attr, char *buf) { return sysfs_emit(buf, "0\n"); }
static DEVICE_ATTR_RO(stable_pages_required);

static struct attribute *bdi_dev_attrs[] = {
	&dev_attr_read_ahead_kb.attr,
	&dev_attr_min_ratio.attr,
	&dev_attr_max_ratio.attr,
	&dev_attr_stable_pages_required.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bdi_dev);

static __init int bdi_class_init(void)
{
	bdi_class = class_create(THIS_MODULE, "bdi");
	if (IS_ERR(bdi_class))
		return PTR_ERR(bdi_class);

	bdi_class->dev_groups = bdi_dev_groups;
	bdi_debug_init();

	return 0;
}
postcore_initcall(bdi_class_init);

static int __init default_bdi_init(void)
{
	bdi_wq = alloc_workqueue("writeback", WQ_MEM_RECLAIM | WQ_UNBOUND |
				 WQ_SYSFS, 0);
	if (!bdi_wq)
		return -ENOMEM;
	return 0;
}
subsys_initcall(default_bdi_init);

/* Stub: wb_wakeup_delayed not used in minimal kernel */
void wb_wakeup_delayed(struct bdi_writeback *wb) { }

static void wb_update_bandwidth_workfn(struct work_struct *work)
{
	struct bdi_writeback *wb = container_of(to_delayed_work(work),
						struct bdi_writeback, bw_dwork);

	wb_update_bandwidth(wb);
}

#define INIT_BW		(100 << (20 - PAGE_SHIFT))

static int wb_init(struct bdi_writeback *wb, struct backing_dev_info *bdi,
		   gfp_t gfp)
{
	int i, err;

	memset(wb, 0, sizeof(*wb));

	wb->bdi = bdi;
	wb->last_old_flush = jiffies;
	INIT_LIST_HEAD(&wb->b_dirty);
	INIT_LIST_HEAD(&wb->b_io);
	INIT_LIST_HEAD(&wb->b_more_io);
	INIT_LIST_HEAD(&wb->b_dirty_time);
	spin_lock_init(&wb->list_lock);

	atomic_set(&wb->writeback_inodes, 0);
	wb->bw_time_stamp = jiffies;
	wb->balanced_dirty_ratelimit = INIT_BW;
	wb->dirty_ratelimit = INIT_BW;
	wb->write_bandwidth = INIT_BW;
	wb->avg_write_bandwidth = INIT_BW;

	spin_lock_init(&wb->work_lock);
	INIT_LIST_HEAD(&wb->work_list);
	INIT_DELAYED_WORK(&wb->dwork, wb_workfn);
	INIT_DELAYED_WORK(&wb->bw_dwork, wb_update_bandwidth_workfn);
	wb->dirty_sleep = jiffies;

	err = fprop_local_init_percpu(&wb->completions, gfp);
	if (err)
		return err;

	for (i = 0; i < NR_WB_STAT_ITEMS; i++) {
		err = percpu_counter_init(&wb->stat[i], 0, gfp);
		if (err)
			goto out_destroy_stat;
	}

	return 0;

out_destroy_stat:
	while (i--)
		percpu_counter_destroy(&wb->stat[i]);
	fprop_local_destroy_percpu(&wb->completions);
	return err;
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
	int i;

	WARN_ON(delayed_work_pending(&wb->dwork));

	for (i = 0; i < NR_WB_STAT_ITEMS; i++)
		percpu_counter_destroy(&wb->stat[i]);

	fprop_local_destroy_percpu(&wb->completions);
}


static int cgwb_bdi_init(struct backing_dev_info *bdi)
{
	return wb_init(&bdi->wb, bdi, GFP_KERNEL);
}

static void cgwb_bdi_unregister(struct backing_dev_info *bdi) { }

static void cgwb_bdi_register(struct backing_dev_info *bdi)
{
	list_add_tail_rcu(&bdi->wb.bdi_node, &bdi->wb_list);
}

static void cgwb_remove_from_bdi_list(struct bdi_writeback *wb)
{
	list_del_rcu(&wb->bdi_node);
}


int bdi_init(struct backing_dev_info *bdi)
{
	int ret;

	bdi->dev = NULL;

	kref_init(&bdi->refcnt);
	bdi->min_ratio = 0;
	bdi->max_ratio = 100;
	bdi->max_prop_frac = FPROP_FRAC_BASE;
	INIT_LIST_HEAD(&bdi->bdi_list);
	INIT_LIST_HEAD(&bdi->wb_list);
	init_waitqueue_head(&bdi->wb_waitq);

	ret = cgwb_bdi_init(bdi);

	return ret;
}

struct backing_dev_info *bdi_alloc(int node_id)
{
	struct backing_dev_info *bdi;

	bdi = kzalloc_node(sizeof(*bdi), GFP_KERNEL, node_id);
	if (!bdi)
		return NULL;

	if (bdi_init(bdi)) {
		kfree(bdi);
		return NULL;
	}
	bdi->capabilities = BDI_CAP_WRITEBACK | BDI_CAP_WRITEBACK_ACCT;
	bdi->ra_pages = VM_READAHEAD_PAGES;
	bdi->io_pages = VM_READAHEAD_PAGES;
	timer_setup(&bdi->laptop_mode_wb_timer, laptop_mode_timer_fn, 0);
	return bdi;
}

static struct rb_node **bdi_lookup_rb_node(u64 id, struct rb_node **parentp)
{
	struct rb_node **p = &bdi_tree.rb_node;
	struct rb_node *parent = NULL;
	struct backing_dev_info *bdi;

	lockdep_assert_held(&bdi_lock);

	while (*p) {
		parent = *p;
		bdi = rb_entry(parent, struct backing_dev_info, rb_node);

		if (bdi->id > id)
			p = &(*p)->rb_left;
		else if (bdi->id < id)
			p = &(*p)->rb_right;
		else
			break;
	}

	if (parentp)
		*parentp = parent;
	return p;
}

/* Stub: bdi_get_by_id not used in minimal kernel */
struct backing_dev_info *bdi_get_by_id(u64 id) { return NULL; }

int bdi_register_va(struct backing_dev_info *bdi, const char *fmt, va_list args)
{
	struct device *dev;
	struct rb_node *parent, **p;

	if (bdi->dev)	 
		return 0;

	vsnprintf(bdi->dev_name, sizeof(bdi->dev_name), fmt, args);
	dev = device_create(bdi_class, NULL, MKDEV(0, 0), bdi, bdi->dev_name);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	cgwb_bdi_register(bdi);
	bdi->dev = dev;

	bdi_debug_register(bdi, dev_name(dev));
	set_bit(WB_registered, &bdi->wb.state);

	spin_lock_bh(&bdi_lock);

	bdi->id = ++bdi_id_cursor;

	p = bdi_lookup_rb_node(bdi->id, &parent);
	rb_link_node(&bdi->rb_node, parent, p);
	rb_insert_color(&bdi->rb_node, &bdi_tree);

	list_add_tail_rcu(&bdi->bdi_list, &bdi_list);

	spin_unlock_bh(&bdi_lock);

	return 0;
}

int bdi_register(struct backing_dev_info *bdi, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = bdi_register_va(bdi, fmt, args);
	va_end(args);
	return ret;
}

/* Stub: bdi_set_owner not used in minimal kernel */
void bdi_set_owner(struct backing_dev_info *bdi, struct device *owner) { }

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
	cgwb_bdi_unregister(bdi);

	 
	if (bdi->min_ratio)
		bdi_set_min_ratio(bdi, 0);

	if (bdi->dev) {
		bdi_debug_unregister(bdi);
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

const char *bdi_dev_name(struct backing_dev_info *bdi)
{
	if (!bdi || !bdi->dev)
		return bdi_unknown_name;
	return bdi->dev_name;
}
