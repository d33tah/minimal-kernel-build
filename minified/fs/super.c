
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
#include <linux/fscrypt.h>
#include <linux/fsnotify.h>
#include <linux/lockdep.h>
#include <linux/user_namespace.h>
#include <linux/fs_context.h>
#include <uapi/linux/mount.h>
#include "internal.h"

static LIST_HEAD(super_blocks);
static DEFINE_SPINLOCK(sb_lock);

static char *sb_writers_name[SB_FREEZE_LEVELS] = {
	"sb_writers",
	"sb_pagefaults",
	"sb_internal",
};

static unsigned long super_cache_scan(struct shrinker *shrink,
				      struct shrink_control *sc)
{
	struct super_block *sb;
	long	fs_objects = 0;
	long	total_objects;
	long	freed = 0;
	long	dentries;
	long	inodes;

	sb = container_of(shrink, struct super_block, s_shrink);

	if (!(sc->gfp_mask & __GFP_FS))
		return SHRINK_STOP;

	if (!trylock_super(sb))
		return SHRINK_STOP;

	if (sb->s_op->nr_cached_objects)
		fs_objects = sb->s_op->nr_cached_objects(sb, sc);

	inodes = list_lru_shrink_count(&sb->s_inode_lru, sc);
	dentries = list_lru_shrink_count(&sb->s_dentry_lru, sc);
	total_objects = dentries + inodes + fs_objects + 1;
	if (!total_objects)
		total_objects = 1;

	dentries = mult_frac(sc->nr_to_scan, dentries, total_objects);
	inodes = mult_frac(sc->nr_to_scan, inodes, total_objects);
	fs_objects = mult_frac(sc->nr_to_scan, fs_objects, total_objects);

	sc->nr_to_scan = dentries + 1;
	freed = prune_dcache_sb(sb, sc);
	sc->nr_to_scan = inodes + 1;
	freed += prune_icache_sb(sb, sc);

	if (fs_objects) {
		sc->nr_to_scan = fs_objects + 1;
		freed += sb->s_op->free_cached_objects(sb, sc);
	}

	up_read(&sb->s_umount);
	return freed;
}

static unsigned long super_cache_count(struct shrinker *shrink,
				       struct shrink_control *sc)
{
	struct super_block *sb;
	long	total_objects = 0;

	sb = container_of(shrink, struct super_block, s_shrink);

	if (!(sb->s_flags & SB_BORN))
		return 0;
	smp_rmb();

	if (sb->s_op && sb->s_op->nr_cached_objects)
		total_objects = sb->s_op->nr_cached_objects(sb, sc);

	total_objects += list_lru_shrink_count(&sb->s_dentry_lru, sc);
	total_objects += list_lru_shrink_count(&sb->s_inode_lru, sc);

	if (!total_objects)
		return SHRINK_EMPTY;

	total_objects = vfs_pressure_ratio(total_objects);
	return total_objects;
}

static void destroy_super_work(struct work_struct *work)
{
	struct super_block *s = container_of(work, struct super_block,
							destroy_work);
	int i;

	for (i = 0; i < SB_FREEZE_LEVELS; i++)
		percpu_free_rwsem(&s->s_writers.rw_sem[i]);
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
	security_sb_free(s);
	put_user_ns(s->s_user_ns);
	kfree(s->s_subtype);
	free_prealloced_shrinker(&s->s_shrink);
	
	destroy_super_work(&s->destroy_work);
}

static struct super_block *alloc_super(struct file_system_type *type, int flags,
				       struct user_namespace *user_ns)
{
	struct super_block *s = kzalloc(sizeof(struct super_block),  GFP_USER);
	static const struct super_operations default_op;
	int i;

	if (!s)
		return NULL;

	INIT_LIST_HEAD(&s->s_mounts);
	s->s_user_ns = get_user_ns(user_ns);
	init_rwsem(&s->s_umount);
	lockdep_set_class(&s->s_umount, &type->s_umount_key);
	
	down_write_nested(&s->s_umount, SINGLE_DEPTH_NESTING);

	if (security_sb_alloc(s))
		goto fail;

	for (i = 0; i < SB_FREEZE_LEVELS; i++) {
		if (__percpu_init_rwsem(&s->s_writers.rw_sem[i],
					sb_writers_name[i],
					&type->s_writers_key[i]))
			goto fail;
	}
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

	s->s_shrink.seeks = DEFAULT_SEEKS;
	s->s_shrink.scan_objects = super_cache_scan;
	s->s_shrink.count_objects = super_cache_count;
	s->s_shrink.batch = 1024;
	s->s_shrink.flags = SHRINKER_NUMA_AWARE | SHRINKER_MEMCG_AWARE;
	if (prealloc_shrinker(&s->s_shrink))
		goto fail;
	if (list_lru_init_memcg(&s->s_dentry_lru, &s->s_shrink))
		goto fail;
	if (list_lru_init_memcg(&s->s_inode_lru, &s->s_shrink))
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
		security_sb_free(s);
		fscrypt_sb_free(s);
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
		unregister_shrinker(&s->s_shrink);
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


static int grab_super(struct super_block *s) __releases(sb_lock)
{
	s->s_count++;
	spin_unlock(&sb_lock);
	down_write(&s->s_umount);
	if ((s->s_flags & SB_BORN) && atomic_inc_not_zero(&s->s_active)) {
		put_super(s);
		return 1;
	}
	up_write(&s->s_umount);
	put_super(s);
	return 0;
}

/* Stub: trylock_super not used in minimal kernel */
bool trylock_super(struct super_block *sb) { return false; }

void generic_shutdown_super(struct super_block *sb)
{
	const struct super_operations *sop = sb->s_op;

	if (sb->s_root) {
		shrink_dcache_for_umount(sb);
		sync_filesystem(sb);
		sb->s_flags &= ~SB_ACTIVE;

		cgroup_writeback_umount();

		evict_inodes(sb);
		
		fsnotify_sb_delete(sb);
		security_sb_delete(sb);

		if (sb->s_dio_done_wq) {
			destroy_workqueue(sb->s_dio_done_wq);
			sb->s_dio_done_wq = NULL;
		}

		if (sop->put_super)
			sop->put_super(sb);

		if (!list_empty(&sb->s_inodes)) {
			printk("VFS: Busy inodes after unmount of %s. "
			   "Self-destruct in 5 seconds.  Have a nice day...\n",
			   sb->s_id);
		}
	}
	spin_lock(&sb_lock);
	
	hlist_del_init(&sb->s_instances);
	spin_unlock(&sb_lock);
	up_write(&sb->s_umount);
	if (sb->s_bdi != &noop_backing_dev_info) {
		if (sb->s_iflags & SB_I_PERSB_BDI)
			bdi_unregister(sb->s_bdi);
		bdi_put(sb->s_bdi);
		sb->s_bdi = &noop_backing_dev_info;
	}
}


bool mount_capable(struct fs_context *fc)
{
	if (!(fc->fs_type->fs_flags & FS_USERNS_MOUNT))
		return capable(CAP_SYS_ADMIN);
	else
		return ns_capable(fc->user_ns, CAP_SYS_ADMIN);
}

struct super_block *sget_fc(struct fs_context *fc,
			    int (*test)(struct super_block *, struct fs_context *),
			    int (*set)(struct super_block *, struct fs_context *))
{
	struct super_block *s = NULL;
	struct super_block *old;
	struct user_namespace *user_ns = fc->global ? &init_user_ns : fc->user_ns;
	int err;

retry:
	spin_lock(&sb_lock);
	if (test) {
		hlist_for_each_entry(old, &fc->fs_type->fs_supers, s_instances) {
			if (test(old, fc))
				goto share_extant_sb;
		}
	}
	if (!s) {
		spin_unlock(&sb_lock);
		s = alloc_super(fc->fs_type, fc->sb_flags, user_ns);
		if (!s)
			return ERR_PTR(-ENOMEM);
		goto retry;
	}

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
	register_shrinker_prepared(&s->s_shrink);
	return s;

share_extant_sb:
	if (user_ns != old->s_user_ns) {
		spin_unlock(&sb_lock);
		destroy_unused_super(s);
		return ERR_PTR(-EBUSY);
	}
	if (!grab_super(old))
		goto retry;
	destroy_unused_super(s);
	return old;
}

struct super_block *sget(struct file_system_type *type,
			int (*test)(struct super_block *,void *),
			int (*set)(struct super_block *,void *),
			int flags,
			void *data)
{
	struct user_namespace *user_ns = current_user_ns();
	struct super_block *s = NULL;
	struct super_block *old;
	int err;

	if (flags & SB_SUBMOUNT)
		user_ns = &init_user_ns;

retry:
	spin_lock(&sb_lock);
	if (test) {
		hlist_for_each_entry(old, &type->fs_supers, s_instances) {
			if (!test(old, data))
				continue;
			if (user_ns != old->s_user_ns) {
				spin_unlock(&sb_lock);
				destroy_unused_super(s);
				return ERR_PTR(-EBUSY);
			}
			if (!grab_super(old))
				goto retry;
			destroy_unused_super(s);
			return old;
		}
	}
	if (!s) {
		spin_unlock(&sb_lock);
		s = alloc_super(type, (flags & ~SB_SUBMOUNT), user_ns);
		if (!s)
			return ERR_PTR(-ENOMEM);
		goto retry;
	}

	err = set(s, data);
	if (err) {
		spin_unlock(&sb_lock);
		destroy_unused_super(s);
		return ERR_PTR(err);
	}
	s->s_type = type;
	strlcpy(s->s_id, type->name, sizeof(s->s_id));
	list_add_tail(&s->s_list, &super_blocks);
	hlist_add_head(&s->s_instances, &type->fs_supers);
	spin_unlock(&sb_lock);
	get_filesystem(type);
	register_shrinker_prepared(&s->s_shrink);
	return s;
}

void drop_super(struct super_block *sb)
{
	up_read(&sb->s_umount);
	put_super(sb);
}


void drop_super_exclusive(struct super_block *sb)
{
	up_write(&sb->s_umount);
	put_super(sb);
}

/* Stub: not used in minimal kernel */
void iterate_supers(void (*f)(struct super_block *, void *), void *arg) { }

/* Stub: not used in minimal kernel */
void iterate_supers_type(struct file_system_type *type,
	void (*f)(struct super_block *, void *), void *arg) { }


/* Stubbed - not used in minimal kernel (no block device) */
struct super_block *get_super(struct block_device *bdev)
{
	return NULL;
}

/* Stubbed - not used in minimal kernel (no block device) */
struct super_block *get_active_super(struct block_device *bdev)
{
	return NULL;
}

/* Stubbed - not used in minimal kernel */
struct super_block *user_get_super(dev_t dev, bool excl)
{
	return NULL;
}

/* Stubbed - remount not needed for minimal kernel */
int reconfigure_super(struct fs_context *fc)
{
	return -EINVAL;
}

/* Stub: emergency remount not needed for minimal kernel */
void emergency_remount(void) { }

/* Stub: emergency thaw not needed for minimal kernel */
void emergency_thaw_all(void)
{
}

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
	if (sb->s_root)
		d_genocide(sb->s_root);
	kill_anon_super(sb);
}

int set_anon_super_fc(struct super_block *sb, struct fs_context *fc)
{
	return set_anon_super(sb, NULL);
}

static int test_keyed_super(struct super_block *sb, struct fs_context *fc)
{
	return sb->s_fs_info == fc->s_fs_info;
}

static int test_single_super(struct super_block *s, struct fs_context *fc)
{
	return 1;
}

int vfs_get_super(struct fs_context *fc,
		  enum vfs_get_super_keying keying,
		  int (*fill_super)(struct super_block *sb,
				    struct fs_context *fc))
{
	int (*test)(struct super_block *, struct fs_context *);
	struct super_block *sb;
	int err;

	switch (keying) {
	case vfs_get_single_super:
	case vfs_get_single_reconf_super:
		test = test_single_super;
		break;
	case vfs_get_keyed_super:
		test = test_keyed_super;
		break;
	case vfs_get_independent_super:
		test = NULL;
		break;
	default:
		BUG();
	}

	sb = sget_fc(fc, test, set_anon_super_fc);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	if (!sb->s_root) {
		err = fill_super(sb, fc);
		if (err)
			goto error;

		sb->s_flags |= SB_ACTIVE;
		fc->root = dget(sb->s_root);
	} else {
		fc->root = dget(sb->s_root);
		if (keying == vfs_get_single_reconf_super) {
			err = reconfigure_super(fc);
			if (err < 0) {
				dput(fc->root);
				fc->root = NULL;
				goto error;
			}
		}
	}

	return 0;

error:
	deactivate_locked_super(sb);
	return err;
}

int get_tree_nodev(struct fs_context *fc,
		  int (*fill_super)(struct super_block *sb,
				    struct fs_context *fc))
{
	return vfs_get_super(fc, vfs_get_independent_super, fill_super);
}

int get_tree_single(struct fs_context *fc,
		  int (*fill_super)(struct super_block *sb,
				    struct fs_context *fc))
{
	return vfs_get_super(fc, vfs_get_single_super, fill_super);
}

int get_tree_single_reconf(struct fs_context *fc,
		  int (*fill_super)(struct super_block *sb,
				    struct fs_context *fc))
{
	return vfs_get_super(fc, vfs_get_single_reconf_super, fill_super);
}

int get_tree_keyed(struct fs_context *fc,
		  int (*fill_super)(struct super_block *sb,
				    struct fs_context *fc),
		void *key)
{
	fc->s_fs_info = key;
	return vfs_get_super(fc, vfs_get_keyed_super, fill_super);
}

struct dentry *mount_nodev(struct file_system_type *fs_type,
	int flags, void *data,
	int (*fill_super)(struct super_block *, void *, int))
{
	int error;
	struct super_block *s = sget(fs_type, NULL, set_anon_super, flags, NULL);

	if (IS_ERR(s))
		return ERR_CAST(s);

	error = fill_super(s, data, flags & SB_SILENT ? 1 : 0);
	if (error) {
		deactivate_locked_super(s);
		return ERR_PTR(error);
	}
	s->s_flags |= SB_ACTIVE;
	return dget(s->s_root);
}

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

	error = security_sb_set_mnt_opts(sb, fc->security, 0, NULL);
	if (unlikely(error)) {
		fc_drop_locked(fc);
		return error;
	}

	WARN((sb->s_maxbytes < 0), "%s set sb->s_maxbytes to "
		"negative value (%lld)\n", fc->fs_type->name, sb->s_maxbytes);

	return 0;
}

/* Stub: super_setup_bdi_name not used externally */
int super_setup_bdi_name(struct super_block *sb, char *fmt, ...)
{
	return 0;
}

/* Stub: super_setup_bdi not used externally */
int super_setup_bdi(struct super_block *sb)
{
	return 0;
}
