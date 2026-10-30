
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/user_namespace.h>
#include <linux/cred.h>
#include <linux/fs_struct.h>
#include <linux/sched/task.h>
#include <linux/fs_context.h>

#include <linux/mount.h>
#include "internal.h"

static struct kmem_cache *mnt_cache __read_mostly;

static struct vfsmount *alloc_vfsmnt(void)
{
	struct vfsmount *mnt = kmem_cache_zalloc(mnt_cache, GFP_KERNEL);
	if (mnt)
		mnt->mnt_userns = &init_user_ns;
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

static struct vfsmount *vfs_kern_mount(struct file_system_type *type)
{
	struct fs_context *fc;
	struct vfsmount *mnt;
	int err;

	fc = fs_context_for_mount(type, 0);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	err = fc->ops->get_tree(fc);
	mnt = err ? NULL : alloc_vfsmnt();
	if (!mnt) {
		put_fs_context(fc);
		return ERR_PTR(err ?: -ENOMEM);
	}
	mnt->mnt_sb = fc->root->d_sb;
	mnt->mnt_root = dget(fc->root);
	if (!initial_idmapping(mnt->mnt_sb->s_user_ns))
		mnt->mnt_userns = get_user_ns(mnt->mnt_sb->s_user_ns);

	put_fs_context(fc);
	return mnt;
}

/*
 * Mounts are never detached or freed (no mount refcounting), and there is
 * one mount namespace-free tree: the rootfs mount.
 */
__latent_entropy static void __init init_mount_tree(void)
{
	struct vfsmount *mnt;
	struct path root;

	mnt = vfs_kern_mount(&rootfs_fs_type);
	if (IS_ERR(mnt))
		panic("Can't create rootfs");

	root.mnt = mnt;
	root.dentry = mnt->mnt_root;
	/* MNT_LOCKED flag dropped: mnt_flags was write-only. */

	set_fs_pwd(current->fs, &root);
	set_fs_root(current->fs, &root);
}

void __init mnt_init(void)
{
	mnt_cache = kmem_cache_create(
		"mnt_cache", sizeof(struct vfsmount), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);

	init_mount_tree();
}
