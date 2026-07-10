
#include <linux/user_namespace.h>
#include <linux/cred.h>
#include <linux/proc_ns.h>
#include <linux/fs_struct.h>	
#include <linux/sched/task.h>
#include <linux/fs_context.h>
#include <linux/shmem_fs.h>

#include "mount.h"
#include "internal.h"


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

static struct mount *alloc_vfsmnt(const char *name)
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

	smp_mb();
	might_lock(&mount_lock.lock);

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

int __legitimize_mnt(struct vfsmount *bastard, unsigned seq)
{
	if (read_seqretry(&mount_lock, seq))
		return 1;
	if (bastard == NULL)
		return 0;
	smp_mb();
	if (likely(!read_seqretry(&mount_lock, seq)))
		return 0;
	/* MNT_SYNC_UMOUNT / MNT_DOOMED never set -> both branches always false. */
	lock_mount_hash();
	unlock_mount_hash();

	return -1;
}

bool legitimize_mnt(struct vfsmount *bastard, unsigned seq)
{
	int res = __legitimize_mnt(bastard, seq);
	if (likely(!res))
		return true;
	if (unlikely(res < 0)) {
		rcu_read_unlock();
		mntput(bastard);
		rcu_read_lock();
	}
	return false;
}

static inline int check_mnt(struct mount *mnt)
{
	return mnt->mnt_ns == current->nsproxy->mnt_ns;
}

struct vfsmount *vfs_create_mount(struct fs_context *fc)
{
	struct mount *mnt;
	struct user_namespace *fs_userns;

	if (!fc->root)
		return ERR_PTR(-EINVAL);

	mnt = alloc_vfsmnt(fc->source ?: "none");
	if (!mnt)
		return ERR_PTR(-ENOMEM);

	/* MNT_INTERNAL flag dropped: mnt_flags was write-only. */

	atomic_inc(&fc->root->d_sb->s_active);
	mnt->mnt.mnt_sb		= fc->root->d_sb;
	mnt->mnt.mnt_root	= dget(fc->root);

	fs_userns = mnt->mnt.mnt_sb->s_user_ns;
	if (!initial_idmapping(fs_userns))
		mnt->mnt.mnt_userns = get_user_ns(fs_userns);

	return &mnt->mnt;
}

struct vfsmount *fc_mount(struct fs_context *fc)
{
	int err = vfs_get_tree(fc);
	if (!err) {
		up_write(&fc->root->d_sb->s_umount);
		return vfs_create_mount(fc);
	}
	return ERR_PTR(err);
}

struct vfsmount *vfs_kern_mount(struct file_system_type *type,
				int flags, const char *name,
				void *data)
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
		ret = vfs_parse_fs_string(fc, "source",
					  name, strlen(name));
	if (!ret)
		ret = parse_monolithic_mount_data(fc, data);
	if (!ret)
		mnt = fc_mount(fc);
	else
		mnt = ERR_PTR(ret);

	put_fs_context(fc);
	return mnt;
}



static void mntput_no_expire(struct mount *mnt)
{
	/*
	 * Every mount on this minimal kernel is attached to a namespace:
	 * the rootfs mount carries the initial mnt_namespace and the shmem
	 * kern_mount carries MNT_NS_INTERNAL, both non-NULL. There is no
	 * umount / move_mount / detach machinery, so no mount is ever put
	 * with mnt_ns == NULL. The teardown tail (refcount-to-zero,
	 * unhash, free) is therefore unreachable; only the decrement-count
	 * fast path runs.
	 */
	rcu_read_lock();
	rcu_read_unlock();
}

void mntput(struct vfsmount *mnt)
{
	if (mnt) {
		struct mount *m = real_mount(mnt);

		mntput_no_expire(m);
	}
}

struct vfsmount *mntget(struct vfsmount *mnt)
{
	return mnt;
}






static struct ucounts *inc_mnt_namespaces(struct user_namespace *ns)
{
	return inc_ucount(ns, current_euid(), UCOUNT_MNT_NAMESPACES);
}

static void dec_mnt_namespaces(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_MNT_NAMESPACES);
}

static struct mnt_namespace *alloc_mnt_ns(struct user_namespace *user_ns, bool anon)
{
	struct mnt_namespace *new_ns;
	struct ucounts *ucounts;
	int ret;

	ucounts = inc_mnt_namespaces(user_ns);
	if (!ucounts)
		return ERR_PTR(-ENOSPC);

	new_ns = kzalloc(sizeof(struct mnt_namespace), GFP_KERNEL_ACCOUNT);
	if (!new_ns) {
		dec_mnt_namespaces(ucounts);
		return ERR_PTR(-ENOMEM);
	}
	if (!anon) {
		ret = ns_alloc_inum(&new_ns->ns);
		if (ret) {
			kfree(new_ns);
			dec_mnt_namespaces(ucounts);
			return ERR_PTR(ret);
		}
	}
	refcount_set(&new_ns->ns.count, 1);
	new_ns->user_ns = get_user_ns(user_ns);
	new_ns->ucounts = ucounts;
	return new_ns;
}

__latent_entropy
static void __init init_mount_tree(void)
{
	struct vfsmount *mnt;
	struct mount *m;
	struct mnt_namespace *ns;
	struct path root;

	mnt = vfs_kern_mount(&rootfs_fs_type, 0, "rootfs", NULL);
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
	mnt_cache = kmem_cache_create("mnt_cache", sizeof(struct mount),
			0, SLAB_HWCACHE_ALIGN|SLAB_PANIC|SLAB_ACCOUNT, NULL);

	/* Stub: fs_kobj not used in minimal kernel */
	shmem_init();
	init_mount_tree();
}

/*
 * put_mnt_ns / free_mnt_ns: cascade-deleted. Their sole live caller chain was
 * free_nsproxy (deleted with exit_task_namespaces); nothing drops a mnt_ns on
 * a 1-shot boot that never clones a namespace.
 */

struct vfsmount *kern_mount(struct file_system_type *type)
{
	struct vfsmount *mnt;
	mnt = vfs_kern_mount(type, SB_KERNMOUNT, type->name, NULL);
	if (!IS_ERR(mnt)) {
		
		real_mount(mnt)->mnt_ns = MNT_NS_INTERNAL;
	}
	return mnt;
}



bool mnt_may_suid(struct vfsmount *mnt)
{
	
	return check_mnt(real_mount(mnt));
}

