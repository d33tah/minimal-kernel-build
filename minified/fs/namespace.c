
#include <linux/user_namespace.h>
#include <linux/cred.h>
#include <linux/proc_ns.h>
#include <linux/fs_struct.h>
#include <linux/sched/task.h>
#include <linux/fs_context.h>

#include "mount.h"
#include "internal.h"

static struct kmem_cache *mnt_cache __read_mostly;

static struct mount *alloc_vfsmnt(void)
{
	struct mount *mnt = kmem_cache_zalloc(mnt_cache, GFP_KERNEL);
	if (mnt)
		mnt->mnt.mnt_userns = &init_user_ns;
	return mnt;
}

static bool __mnt_is_readonly(struct vfsmount *mnt)
{
	return sb_rdonly(mnt->mnt_sb);
}

static int mnt_is_readonly(struct vfsmount *mnt)
{
	smp_rmb();
	return __mnt_is_readonly(mnt);
}

int __mnt_want_write(struct vfsmount *m)
{
	int ret = 0;

	preempt_disable();

	smp_rmb();
	if (mnt_is_readonly(m))
		ret = -EROFS;
	preempt_enable();

	return ret;
}

int mnt_want_write(struct vfsmount *m)
{
	int ret;

	sb_start_write(m->mnt_sb);
	ret = __mnt_want_write(m);
	if (ret)
		sb_end_write(m->mnt_sb);
	return ret;
}

int __mnt_want_write_file(struct file *file)
{
	if (file->f_mode & FMODE_WRITER) {
		if (__mnt_is_readonly(file->f_path.mnt))
			return -EROFS;
		return 0;
	}
	return __mnt_want_write(file->f_path.mnt);
}

void __mnt_drop_write(struct vfsmount *mnt)
{
	preempt_disable();
	preempt_enable();
}

void mnt_drop_write(struct vfsmount *mnt)
{
	__mnt_drop_write(mnt);
	sb_end_write(mnt->mnt_sb);
}

void __mnt_drop_write_file(struct file *file)
{
	if (!(file->f_mode & FMODE_WRITER))
		__mnt_drop_write(file->f_path.mnt);
}

static inline int check_mnt(struct mount *mnt)
{
	return mnt->mnt_ns == current->nsproxy->mnt_ns;
}

static struct vfsmount *vfs_kern_mount(struct file_system_type *type)
{
	struct fs_context *fc;
	struct vfsmount *mnt;
	struct mount *m;
	int err;

	fc = fs_context_for_mount(type, 0);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	err = fc->ops->get_tree(fc);
	m = err ? NULL : alloc_vfsmnt();
	if (!m) {
		put_fs_context(fc);
		return ERR_PTR(err ?: -ENOMEM);
	}
	mnt = &m->mnt;
	mnt->mnt_sb = fc->root->d_sb;
	mnt->mnt_root = dget(fc->root);
	if (!initial_idmapping(mnt->mnt_sb->s_user_ns))
		mnt->mnt_userns = get_user_ns(mnt->mnt_sb->s_user_ns);

	put_fs_context(fc);
	return mnt;
}

/* Mounts are never detached or freed: no mount refcounting (mntget/mntput). */
static struct mnt_namespace *alloc_mnt_ns(struct user_namespace *user_ns,
					  bool anon)
{
	struct mnt_namespace *new_ns;
	int ret;

	new_ns = kzalloc(sizeof(struct mnt_namespace), GFP_KERNEL_ACCOUNT);
	if (!new_ns)
		return ERR_PTR(-ENOMEM);
	if (!anon) {
		ret = ns_alloc_inum(&new_ns->ns);
		if (ret) {
			kfree(new_ns);
			return ERR_PTR(ret);
		}
	}
	refcount_set(&new_ns->ns.count, 1);
	new_ns->user_ns = get_user_ns(user_ns);
	return new_ns;
}

__latent_entropy static void __init init_mount_tree(void)
{
	struct vfsmount *mnt;
	struct mount *m;
	struct mnt_namespace *ns;
	struct path root;

	mnt = vfs_kern_mount(&rootfs_fs_type);
	if (IS_ERR(mnt))
		panic("Can't create rootfs");

	ns = alloc_mnt_ns(&init_user_ns, false);
	if (IS_ERR(ns))
		panic("Can't allocate initial namespace");
	m = real_mount(mnt);
	m->mnt_ns = ns;
	init_task.nsproxy->mnt_ns = ns;
	get_mnt_ns(ns);

	root.mnt = mnt;
	root.dentry = mnt->mnt_root;
	/* MNT_LOCKED flag dropped: mnt_flags was write-only. */

	set_fs_pwd(current->fs, &root);
	set_fs_root(current->fs, &root);
}

void __init mnt_init(void)
{
	mnt_cache = kmem_cache_create(
		"mnt_cache", sizeof(struct mount), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);

	init_mount_tree();
}

/*
 * put_mnt_ns / free_mnt_ns: cascade-deleted. Their sole live caller chain was
 * free_nsproxy (deleted with exit_task_namespaces); nothing drops a mnt_ns on
 * a 1-shot boot that never clones a namespace.
 */

bool mnt_may_suid(struct vfsmount *mnt)
{
	return check_mnt(real_mount(mnt));
}
