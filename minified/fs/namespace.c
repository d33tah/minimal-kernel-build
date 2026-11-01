#include <linux/fs.h>
#include <linux/user_namespace.h>
#include <linux/fs_struct.h>
#include <linux/fs_context.h>
#include "internal.h"

static struct kmem_cache *mnt_cache __read_mostly;

static struct vfsmount *alloc_vfsmnt(void)
{
	struct vfsmount *mnt = kzalloc(sizeof(*mnt), GFP_KERNEL);
	if (mnt)
		mnt->mnt_userns = &init_user_ns;
	return mnt;
}

static struct vfsmount *vfs_kern_mount(struct file_system_type *type)
{
	struct fs_context *fc;
	struct vfsmount *mnt;
	int err;

	fc = fs_context_for_mount(type);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	err = fc->ops->get_tree(fc);
	mnt = err ? NULL : alloc_vfsmnt();
	if (!mnt) {
		put_fs_context(fc);
		return ERR_PTR(err ?: -ENOMEM);
	}
	mnt->mnt_root = dget(fc->root);

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
