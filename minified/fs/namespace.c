#include <linux/user_namespace.h>
#include <linux/idr.h>
/* sysfs.h inlined */
#include <linux/stat.h>
struct attribute {
	const char *name;
	umode_t mode;
};
struct attribute_group {
	struct attribute **attrs;
};
#include <linux/fs_struct.h>
#include <linux/fs_parser.h>

/* mount.h inlined */
struct mnt_namespace {
	struct ns_common ns;
	struct mount *root;
	struct list_head list;
	struct user_namespace *user_ns;
	unsigned int mounts;
} __randomize_layout;

struct mount {
	struct vfsmount mnt;
	int mnt_count;
	int mnt_writers;
	const char *mnt_devname;
	struct list_head mnt_list;
	struct mnt_namespace *mnt_ns;
	int mnt_id;
} __randomize_layout;

#define MNT_NS_INTERNAL ERR_PTR(-EINVAL)

static inline struct mount *real_mount(struct vfsmount *mnt)
{
	return container_of(mnt, struct mount, mnt);
}

extern seqlock_t mount_lock;

#include "internal.h"

static DEFINE_IDA(mnt_id_ida);

static struct kmem_cache *mnt_cache __read_mostly;

__cacheline_aligned_in_smp DEFINE_SEQLOCK(mount_lock);

static inline void lock_mount_hash(void)
{
	write_seqlock(&mount_lock);
}

static inline void unlock_mount_hash(void)
{
	write_sequnlock(&mount_lock);
}

static void mnt_free_id(struct mount *mnt)
{
	ida_free(&mnt_id_ida, mnt->mnt_id);
}

static inline void mnt_add_count(struct mount *mnt, int n)
{
	preempt_disable();
	mnt->mnt_count += n;
	preempt_enable();
}

static struct mount *alloc_vfsmnt(const char *name)
{
	struct mount *mnt = kmem_cache_zalloc(mnt_cache, GFP_KERNEL);
	if (mnt) {
		int res;

		res = ida_alloc_range(&mnt_id_ida, 0, ~0, GFP_KERNEL);
		if (res < 0)
			goto out_free_cache;
		mnt->mnt_id = res;

		if (name) {
			mnt->mnt_devname =
				kstrdup_const(name, GFP_KERNEL_ACCOUNT);
			if (!mnt->mnt_devname)
				goto out_free_id;
		}

		mnt->mnt_count = 1;
		INIT_LIST_HEAD(&mnt->mnt_list);
	}
	return mnt;

out_free_id:
	mnt_free_id(mnt);
out_free_cache:
	kmem_cache_free(mnt_cache, mnt);
	return NULL;
}

int __mnt_want_write(struct vfsmount *m)
{
	real_mount(m)->mnt_writers++;
	return 0;
}

void __mnt_drop_write(struct vfsmount *mnt)
{
	real_mount(mnt)->mnt_writers--;
}

static struct vfsmount *vfs_create_mount(struct fs_context *fc)
{
	struct mount *mnt;

	if (!fc->root)
		return ERR_PTR(-EINVAL);

	mnt = alloc_vfsmnt(fc->source ?: "none");
	if (!mnt)
		return ERR_PTR(-ENOMEM);

	if (fc->sb_flags & SB_KERNMOUNT)
		mnt->mnt.mnt_flags = MNT_INTERNAL;

	atomic_inc(&fc->root->d_sb->s_active);
	mnt->mnt.mnt_sb = fc->root->d_sb;
	mnt->mnt.mnt_root = dget(fc->root);

	lock_mount_hash();
	unlock_mount_hash();
	return &mnt->mnt;
}

static struct vfsmount *fc_mount(struct fs_context *fc)
{
	int err = vfs_get_tree(fc);
	if (!err) {
		up_write(&fc->root->d_sb->s_umount);
		return vfs_create_mount(fc);
	}
	return ERR_PTR(err);
}

static struct vfsmount *vfs_kern_mount(struct file_system_type *type, int flags,
				       const char *name)
{
	struct fs_context *fc;
	struct vfsmount *mnt;
	int ret = 0;

	if (!type)
		return ERR_PTR(-EINVAL);

	fc = fs_context_for_mount(type, flags);
	if (IS_ERR(fc))
		return ERR_CAST(fc);

	if (name)
		ret = vfs_parse_fs_string(fc, "source", name, strlen(name));
	if (!ret)
		ret = parse_monolithic_mount_data(fc, NULL);
	if (!ret)
		mnt = fc_mount(fc);
	else
		mnt = ERR_PTR(ret);

	put_fs_context(fc);
	return mnt;
}

void mntput(struct vfsmount *mnt)
{
	if (!mnt)
		return;
	mnt_add_count(real_mount(mnt), -1);
}

struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		mnt_add_count(real_mount(mnt), 1);
	return mnt;
}

static struct mnt_namespace *alloc_mnt_ns(struct user_namespace *user_ns)
{
	struct mnt_namespace *new_ns;

	new_ns = kzalloc(sizeof(struct mnt_namespace), GFP_KERNEL_ACCOUNT);
	if (!new_ns)
		return ERR_PTR(-ENOMEM);
	new_ns->ns.inum = 1;
	refcount_set(&new_ns->ns.count, 1);
	INIT_LIST_HEAD(&new_ns->list);
	new_ns->user_ns = get_user_ns(user_ns);
	return new_ns;
}

/* inlined from mm/shmem.c */
extern int ramfs_init_fs_context(struct fs_context *fc);

static struct file_system_type shmem_fs_type = {
	.name = "tmpfs",
	.init_fs_context = ramfs_init_fs_context,
	.kill_sb = kill_litter_super,
};

static struct vfsmount *kern_mount(struct file_system_type *type);

static void __init shmem_init(void)
{
	register_filesystem(&shmem_fs_type);
	kern_mount(&shmem_fs_type);
}

void __init mnt_init(void)
{
	mnt_cache = kmem_cache_create(
		"mnt_cache", sizeof(struct mount), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);

	shmem_init();
	{
		struct vfsmount *mnt;
		struct mount *m;
		struct mnt_namespace *ns;
		struct path root;

		mnt = vfs_kern_mount(&rootfs_fs_type, 0, "rootfs");
		if (IS_ERR(mnt))
			panic("Can't create rootfs");

		ns = alloc_mnt_ns(&init_user_ns);
		if (IS_ERR(ns))
			panic("Can't allocate initial namespace");
		m = real_mount(mnt);
		m->mnt_ns = ns;
		ns->root = m;
		ns->mounts = 1;
		list_add(&m->mnt_list, &ns->list);
		init_task.nsproxy->mnt_ns = ns;
		refcount_inc(&ns->ns.count);

		root.mnt = mnt;
		root.dentry = mnt->mnt_root;
		mnt->mnt_flags |= MNT_LOCKED;

		set_fs_pwd(current->fs, &root);
		set_fs_root(current->fs, &root);
	}
}

static struct vfsmount *kern_mount(struct file_system_type *type)
{
	struct vfsmount *mnt;
	mnt = vfs_kern_mount(type, SB_KERNMOUNT, type->name);
	if (!IS_ERR(mnt)) {
		real_mount(mnt)->mnt_ns = MNT_NS_INTERNAL;
	}
	return mnt;
}
