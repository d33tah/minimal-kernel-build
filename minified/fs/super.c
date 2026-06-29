
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/rculist_bl.h>
#include <linux/user_namespace.h>
#include <linux/fs_context.h>
#include "internal.h"

static DEFINE_SPINLOCK(sb_lock);

/*
 * Runtime-dead on a 1-shot boot: the only callers are the alloc-failure error
 * paths of alloc_super() / sget_fc(), which never fire on a boot whose early
 * allocations succeed (and would panic, not recover, if they didn't). The
 * superblock-release body (up_write/list_lru_destroy/put_user_ns/kfree-via-
 * destroy_super_work) is therefore never executed. Stubbed to keep the symbol
 * for the two error-path call sites; the private subtree (destroy_super_work,
 * list_lru_destroy) cascaded away.
 */
static void destroy_unused_super(struct super_block *s)
{
}

static struct super_block *alloc_super(struct file_system_type *type, int flags,
				       struct user_namespace *user_ns)
{
	struct super_block *s = kzalloc(sizeof(struct super_block),  GFP_USER);
	static const struct super_operations default_op;

	if (!s)
		return NULL;

	s->s_user_ns = get_user_ns(user_ns);
	init_rwsem(&s->s_umount);
	down_write_nested(&s->s_umount, SINGLE_DEPTH_NESTING);

	s->s_flags = flags;
	if (s->s_user_ns != &init_user_ns)
		s->s_iflags |= SB_I_NODEV;
	INIT_HLIST_BL_HEAD(&s->s_roots);

	s->s_count = 1;
	atomic_set(&s->s_active, 1);
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

/*
 * Runtime-dead on a 1-shot boot: the boot filesystems are never unmounted, so
 * s_active never drops to 0 and the umount-only teardown body never runs
 * (deactivate_locked_super HIT=False; deactivate_super takes the
 * atomic_add_unless fast path; __cleanup_mnt is umount-only). Stubbed to keep
 * the symbol for the fs.h extern + kill_sb function-pointer tables. The private
 * subtree (__put_super -> destroy_super_rcu) cascaded away.
 */
void deactivate_locked_super(struct super_block *s)
{
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
struct super_block *sget_fc(struct fs_context *fc,
			    int (*set)(struct super_block *, struct fs_context *))
{
	struct super_block *s;
	struct user_namespace *user_ns = fc->user_ns;
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
	strlcpy(s->s_id, s->s_type->name, sizeof(s->s_id));
	spin_unlock(&sb_lock);
	get_filesystem(s->s_type);
	return s;
}

/* Removed: sget, drop_super, drop_super_exclusive, iterate_supers,
   iterate_supers_type, get_super, get_active_super, user_get_super -
   never called */


static DEFINE_IDA(unnamed_dev_ida);

void kill_litter_super(struct super_block *sb)
{
	/* TEARDOWN-CALLBACK-NEVER-FIRES anchor-stub: the .kill_sb superblock
	 * teardown fn-ptr (shmem + rootfs fs_types). A boot-once artifact never
	 * unmounts, so this never runs (HIT=False). void no-op is behavior-
	 * preserving; the unnamed-dev IDA id release is moot (never reused). */
}

int set_anon_super_fc(struct super_block *sb, struct fs_context *fc)
{
	int dev;

	dev = ida_alloc_range(&unnamed_dev_ida, 1, (1 << MINORBITS) - 1,
			GFP_ATOMIC);
	if (dev == -ENOSPC)
		dev = -EMFILE;
	if (dev < 0)
		return dev;

	sb->s_dev = MKDEV(0, dev);
	return 0;
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

	WARN((sb->s_maxbytes < 0), "%s set sb->s_maxbytes to "
		"negative value (%lld)\n", fc->fs_type->name, sb->s_maxbytes);

	return 0;
}
