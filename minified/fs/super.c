
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/writeback.h>		
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/backing-dev.h>
#include <linux/rculist_bl.h>
#include <linux/lockdep.h>
#include <linux/user_namespace.h>
#include <linux/fs_context.h>
#include <uapi/linux/mount.h>
#include "internal.h"

static LIST_HEAD(super_blocks);
static DEFINE_SPINLOCK(sb_lock);

static void destroy_super_work(struct work_struct *work)
{
	struct super_block *s = container_of(work, struct super_block,
							destroy_work);

	kfree(s);
}

static void destroy_super_rcu(struct rcu_head *head)
{
	struct super_block *s = container_of(head, struct super_block, rcu);
	INIT_WORK(&s->destroy_work, destroy_super_work);
	schedule_work(&s->destroy_work);
}

static void destroy_unused_super(struct super_block *s)
{
	if (!s)
		return;
	up_write(&s->s_umount);
	list_lru_destroy(&s->s_dentry_lru);
	list_lru_destroy(&s->s_inode_lru);
	put_user_ns(s->s_user_ns);
	kfree(s->s_subtype);
	destroy_super_work(&s->destroy_work);
}

static struct super_block *alloc_super(struct file_system_type *type, int flags,
				       struct user_namespace *user_ns)
{
	struct super_block *s = kzalloc(sizeof(struct super_block),  GFP_USER);
	static const struct super_operations default_op;

	if (!s)
		return NULL;

	INIT_LIST_HEAD(&s->s_mounts);
	s->s_user_ns = get_user_ns(user_ns);
	init_rwsem(&s->s_umount);
	lockdep_set_class(&s->s_umount, &type->s_umount_key);
	
	down_write_nested(&s->s_umount, SINGLE_DEPTH_NESTING);

	init_waitqueue_head(&s->s_writers.wait_unfrozen);
	s->s_bdi = &noop_backing_dev_info;
	s->s_flags = flags;
	if (s->s_user_ns != &init_user_ns)
		s->s_iflags |= SB_I_NODEV;
	INIT_HLIST_NODE(&s->s_instances);
	INIT_HLIST_BL_HEAD(&s->s_roots);
	mutex_init(&s->s_sync_lock);
	INIT_LIST_HEAD(&s->s_inodes);
	spin_lock_init(&s->s_inode_list_lock);
	INIT_LIST_HEAD(&s->s_inodes_wb);
	spin_lock_init(&s->s_inode_wblist_lock);

	s->s_count = 1;
	atomic_set(&s->s_active, 1);
	mutex_init(&s->s_vfs_rename_mutex);
	lockdep_set_class(&s->s_vfs_rename_mutex, &type->s_vfs_rename_key);
	init_rwsem(&s->s_dquot.dqio_sem);
	s->s_maxbytes = MAX_NON_LFS;
	s->s_op = &default_op;
	s->s_time_gran = 1000000000;
	s->s_time_min = TIME64_MIN;
	s->s_time_max = TIME64_MAX;

	if (list_lru_init_memcg(&s->s_dentry_lru, NULL))
		goto fail;
	if (list_lru_init_memcg(&s->s_inode_lru, NULL))
		goto fail;
	return s;

fail:
	destroy_unused_super(s);
	return NULL;
}

static void __put_super(struct super_block *s)
{
	if (!--s->s_count) {
		list_del_init(&s->s_list);
		WARN_ON(s->s_dentry_lru.node);
		WARN_ON(s->s_inode_lru.node);
		WARN_ON(!list_empty(&s->s_mounts));
		put_user_ns(s->s_user_ns);
		kfree(s->s_subtype);
		call_rcu(&s->rcu, destroy_super_rcu);
	}
}

void put_super(struct super_block *sb)
{
	spin_lock(&sb_lock);
	__put_super(sb);
	spin_unlock(&sb_lock);
}

void deactivate_locked_super(struct super_block *s)
{
	struct file_system_type *fs = s->s_type;
	if (atomic_dec_and_test(&s->s_active)) {
		fs->kill_sb(s);

		list_lru_destroy(&s->s_dentry_lru);
		list_lru_destroy(&s->s_inode_lru);

		put_filesystem(fs);
		put_super(s);
	} else {
		up_write(&s->s_umount);
	}
}


void deactivate_super(struct super_block *s)
{
	if (!atomic_add_unless(&s->s_active, -1, 1)) {
		down_write(&s->s_umount);
		deactivate_locked_super(s);
	}
}


/*
 * generic_shutdown_super() runs only on superblock DESTRUCTION, reached via
 * fs->kill_sb() -> deactivate_locked_super (dispatched at deactivate_locked_super
 * only when s_active drops to 0). This minimal kernel never unmounts the boot
 * filesystems, so this is unreachable; the umount-only teardown body (dcache/
 * inode eviction, dio workqueue, put_super) is dead and has been removed. Only
 * the sb-list unlink tail is kept so the symbol still links for the kill_sb
 * function-pointer table entries.
 */
void generic_shutdown_super(struct super_block *sb)
{
	spin_lock(&sb_lock);

	hlist_del_init(&sb->s_instances);
	spin_unlock(&sb_lock);
	up_write(&sb->s_umount);
}

struct super_block *sget_fc(struct fs_context *fc,
			    int (*set)(struct super_block *, struct fs_context *))
{
	struct super_block *s;
	struct user_namespace *user_ns = fc->global ? &init_user_ns : fc->user_ns;
	int err;

	s = alloc_super(fc->fs_type, fc->sb_flags, user_ns);
	if (!s)
		return ERR_PTR(-ENOMEM);

	spin_lock(&sb_lock);
	s->s_fs_info = fc->s_fs_info;
	err = set(s, fc);
	if (err) {
		s->s_fs_info = NULL;
		spin_unlock(&sb_lock);
		destroy_unused_super(s);
		return ERR_PTR(err);
	}
	fc->s_fs_info = NULL;
	s->s_type = fc->fs_type;
	s->s_iflags |= fc->s_iflags;
	strlcpy(s->s_id, s->s_type->name, sizeof(s->s_id));
	list_add_tail(&s->s_list, &super_blocks);
	hlist_add_head(&s->s_instances, &s->s_type->fs_supers);
	spin_unlock(&sb_lock);
	get_filesystem(s->s_type);
	return s;
}

/* Removed: sget, drop_super, drop_super_exclusive, iterate_supers,
   iterate_supers_type, get_super, get_active_super, user_get_super -
   never called */


static DEFINE_IDA(unnamed_dev_ida);

int get_anon_bdev(dev_t *p)
{
	int dev;

	dev = ida_alloc_range(&unnamed_dev_ida, 1, (1 << MINORBITS) - 1,
			GFP_ATOMIC);
	if (dev == -ENOSPC)
		dev = -EMFILE;
	if (dev < 0)
		return dev;

	*p = MKDEV(0, dev);
	return 0;
}

void free_anon_bdev(dev_t dev)
{
	ida_free(&unnamed_dev_ida, MINOR(dev));
}

int set_anon_super(struct super_block *s, void *data)
{
	return get_anon_bdev(&s->s_dev);
}

void kill_anon_super(struct super_block *sb)
{
	dev_t dev = sb->s_dev;
	generic_shutdown_super(sb);
	free_anon_bdev(dev);
}

void kill_litter_super(struct super_block *sb)
{
	kill_anon_super(sb);
}

int set_anon_super_fc(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super(sb, NULL);
}

int get_tree_nodev(struct fs_context *fc,
		  int (*fill_super)(struct super_block *sb,
				    struct fs_context *fc))
{
	struct super_block *sb;
	int err;

	sb = sget_fc(fc, set_anon_super_fc);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	err = fill_super(sb, fc);
	if (err) {
		deactivate_locked_super(sb);
		return err;
	}

	sb->s_flags |= SB_ACTIVE;
	fc->root = dget(sb->s_root);
	return 0;
}


/* Removed: mount_nodev - never called (~7 LOC) */

int vfs_get_tree(struct fs_context *fc)
{
	struct super_block *sb;
	int error;

	if (fc->root)
		return -EBUSY;

	error = fc->ops->get_tree(fc);
	if (error < 0)
		return error;

	if (!fc->root) {
		pr_err("Filesystem %s get_tree() didn't set fc->root\n",
		       fc->fs_type->name);
		
		BUG();
	}

	sb = fc->root->d_sb;
	WARN_ON(!sb->s_bdi);

	smp_wmb();
	sb->s_flags |= SB_BORN;


	WARN((sb->s_maxbytes < 0), "%s set sb->s_maxbytes to "
		"negative value (%lld)\n", fc->fs_type->name, sb->s_maxbytes);

	return 0;
}
