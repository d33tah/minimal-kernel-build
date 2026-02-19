struct mnt_namespace;
extern void put_mnt_ns(struct mnt_namespace *ns);
#include <linux/user_namespace.h>
#include <linux/idr.h>
#include <linux/sysfs.h>
#include <linux/fs_struct.h>
#include <linux/proc_ns.h>
#include <linux/memblock.h>
#include <uapi/linux/mount.h>
#include <linux/fs_context.h>
extern void shmem_init(void);

#include "mount.h"
static int mnt_get_count(struct mount *mnt);
static void mnt_set_mountpoint(struct mount *, struct mountpoint *,
			       struct mount *);
/* end pnode.h */
#include "internal.h"

static unsigned int m_hash_mask __read_mostly;
static unsigned int m_hash_shift __read_mostly;
static unsigned int mp_hash_mask __read_mostly;
static unsigned int mp_hash_shift __read_mostly;

static __initdata unsigned long mhash_entries;

static __initdata unsigned long mphash_entries;

static DEFINE_IDA(mnt_id_ida);

static struct hlist_head *mount_hashtable __read_mostly;
static struct hlist_head *mountpoint_hashtable __read_mostly;
static struct kmem_cache *mnt_cache __read_mostly;
static DECLARE_RWSEM(namespace_sem);
static LIST_HEAD(ex_mountpoints);

__cacheline_aligned_in_smp DEFINE_SEQLOCK(mount_lock);

static inline void lock_mount_hash(void)
{
	write_seqlock(&mount_lock);
}

static inline void unlock_mount_hash(void)
{
	write_sequnlock(&mount_lock);
}

static inline struct hlist_head *m_hash(struct vfsmount *mnt,
					struct dentry *dentry)
{
	unsigned long tmp = ((unsigned long)mnt / L1_CACHE_BYTES);
	tmp += ((unsigned long)dentry / L1_CACHE_BYTES);
	tmp = tmp + (tmp >> m_hash_shift);
	return &mount_hashtable[tmp & m_hash_mask];
}

static inline struct hlist_head *mp_hash(struct dentry *dentry)
{
	unsigned long tmp = ((unsigned long)dentry / L1_CACHE_BYTES);
	tmp = tmp + (tmp >> mp_hash_shift);
	return &mountpoint_hashtable[tmp & mp_hash_mask];
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

static int mnt_get_count(struct mount *mnt)
{
	return mnt->mnt_count;
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
		mnt->mnt_writers = 0;

		INIT_HLIST_NODE(&mnt->mnt_hash);
		INIT_LIST_HEAD(&mnt->mnt_child);
		INIT_LIST_HEAD(&mnt->mnt_mounts);
		INIT_LIST_HEAD(&mnt->mnt_list);
		INIT_LIST_HEAD(&mnt->mnt_expire);
		INIT_LIST_HEAD(&mnt->mnt_share);
		INIT_LIST_HEAD(&mnt->mnt_slave_list);
		INIT_LIST_HEAD(&mnt->mnt_slave);
		INIT_HLIST_NODE(&mnt->mnt_mp_list);
		INIT_LIST_HEAD(&mnt->mnt_umounting);
		INIT_HLIST_HEAD(&mnt->mnt_stuck_children);
		mnt->mnt.mnt_userns = &init_user_ns;
	}
	return mnt;

out_free_id:
	mnt_free_id(mnt);
out_free_cache:
	kmem_cache_free(mnt_cache, mnt);
	return NULL;
}

static inline void mnt_dec_writers(struct mount *mnt)
{
	mnt->mnt_writers--;
}

int __mnt_want_write(struct vfsmount *m)
{
	struct mount *mnt = real_mount(m);
	int ret = 0;

	preempt_disable();
	mnt->mnt_writers++;

	smp_mb();
	/* CONFIG_PREEMPT_RT not enabled */
	while (READ_ONCE(mnt->mnt.mnt_flags) & MNT_WRITE_HOLD)
		cpu_relax();

	smp_rmb();
	if ((m->mnt_flags & MNT_READONLY) || (m->mnt_sb->s_flags & SB_RDONLY)) {
		mnt_dec_writers(mnt);
		ret = -EROFS;
	}
	preempt_enable();

	return ret;
}

void __mnt_drop_write(struct vfsmount *mnt)
{
	preempt_disable();
	mnt_dec_writers(real_mount(mnt));
	preempt_enable();
}

static void delayed_free_vfsmnt(struct rcu_head *head)
{
	struct mount *mnt = container_of(head, struct mount, mnt_rcu);
	struct user_namespace *mnt_userns;

	mnt_userns = mnt_user_ns(&mnt->mnt);
	if (!initial_idmapping(mnt_userns))
		put_user_ns(mnt_userns);
	kfree_const(mnt->mnt_devname);
	kmem_cache_free(mnt_cache, mnt);
}

int __legitimize_mnt(struct vfsmount *bastard, unsigned seq)
{
	struct mount *mnt;
	if (read_seqretry(&mount_lock, seq))
		return 1;
	if (bastard == NULL)
		return 0;
	mnt = real_mount(bastard);
	mnt_add_count(mnt, 1);
	smp_mb();
	if (likely(!read_seqretry(&mount_lock, seq)))
		return 0;
	if (bastard->mnt_flags & MNT_SYNC_UMOUNT) {
		mnt_add_count(mnt, -1);
		return 1;
	}
	lock_mount_hash();
	if (unlikely(bastard->mnt_flags & MNT_DOOMED)) {
		mnt_add_count(mnt, -1);
		unlock_mount_hash();
		return 1;
	}
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

struct mount *__lookup_mnt(struct vfsmount *mnt, struct dentry *dentry)
{
	struct hlist_head *head = m_hash(mnt, dentry);
	struct mount *p;

	hlist_for_each_entry_rcu(p, head, mnt_hash)
		if (&p->mnt_parent->mnt == mnt && p->mnt_mountpoint == dentry)
			return p;
	return NULL;
}

static struct vfsmount *lookup_mnt(const struct path *path)
{
	struct mount *child_mnt;
	struct vfsmount *m;
	unsigned seq;

	rcu_read_lock();
	do {
		seq = read_seqbegin(&mount_lock);
		child_mnt = __lookup_mnt(path->mnt, path->dentry);
		m = child_mnt ? &child_mnt->mnt : NULL;
	} while (!legitimize_mnt(m, seq));
	rcu_read_unlock();
	return m;
}

static struct mountpoint *get_mountpoint(struct dentry *dentry)
{
	struct mountpoint *mp, *new = NULL;
	int ret;

	if (d_mountpoint(dentry)) {
		struct hlist_head *chain;

		if (d_unlinked(dentry))
			return ERR_PTR(-ENOENT);
mountpoint:
		read_seqlock_excl(&mount_lock);
		mp = NULL;
		chain = mp_hash(dentry);
		hlist_for_each_entry(mp, chain, m_hash) {
			if (mp->m_dentry == dentry) {
				mp->m_count++;
				break;
			}
		}
		if (mp && mp->m_dentry != dentry)
			mp = NULL;
		read_sequnlock_excl(&mount_lock);
		if (mp)
			goto done;
	}

	if (!new)
		new = kmalloc(sizeof(struct mountpoint), GFP_KERNEL);
	if (!new)
		return ERR_PTR(-ENOMEM);

	ret = d_set_mounted(dentry);

	if (ret == -EBUSY)
		goto mountpoint;

	mp = ERR_PTR(ret);
	if (ret)
		goto done;

	read_seqlock_excl(&mount_lock);
	new->m_dentry = dget(dentry);
	new->m_count = 1;
	hlist_add_head(&new->m_hash, mp_hash(dentry));
	INIT_HLIST_HEAD(&new->m_list);
	read_sequnlock_excl(&mount_lock);

	mp = new;
	new = NULL;
done:
	kfree(new);
	return mp;
}

static void __put_mountpoint(struct mountpoint *mp, struct list_head *list)
{
	if (!--mp->m_count) {
		struct dentry *dentry = mp->m_dentry;
		BUG_ON(!hlist_empty(&mp->m_list));
		spin_lock(&dentry->d_lock);
		dentry->d_flags &= ~DCACHE_MOUNTED;
		spin_unlock(&dentry->d_lock);
		dput_to_list(dentry, list);
		hlist_del(&mp->m_hash);
		kfree(mp);
	}
}

static void put_mountpoint(struct mountpoint *mp)
{
	__put_mountpoint(mp, &ex_mountpoints);
}

static void mnt_set_mountpoint(struct mount *mnt, struct mountpoint *mp,
			       struct mount *child_mnt)
{
	mp->m_count++;
	mnt_add_count(mnt, 1);
	child_mnt->mnt_mountpoint = mp->m_dentry;
	child_mnt->mnt_parent = mnt;
	child_mnt->mnt_mp = mp;
	hlist_add_head(&child_mnt->mnt_mp_list, &mp->m_list);
}

static void __attach_mnt(struct mount *mnt, struct mount *parent)
{
	hlist_add_head_rcu(&mnt->mnt_hash,
			   m_hash(&parent->mnt, mnt->mnt_mountpoint));
	list_add_tail(&mnt->mnt_child, &parent->mnt_mounts);
}

static struct vfsmount *vfs_create_mount(struct fs_context *fc)
{
	struct mount *mnt;
	struct user_namespace *fs_userns;

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
	mnt->mnt_mountpoint = mnt->mnt.mnt_root;
	mnt->mnt_parent = mnt;

	fs_userns = mnt->mnt.mnt_sb->s_user_ns;
	if (!initial_idmapping(fs_userns))
		mnt->mnt.mnt_userns = get_user_ns(fs_userns);

	lock_mount_hash();
	list_add_tail(&mnt->mnt_instance, &mnt->mnt.mnt_sb->s_mounts);
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

static void cleanup_mnt(struct mount *mnt)
{
	struct hlist_node *p;
	struct mount *m;

	WARN_ON(mnt->mnt_writers);
	hlist_for_each_entry_safe(m, p, &mnt->mnt_stuck_children, mnt_umount) {
		hlist_del(&m->mnt_umount);
		mntput(&m->mnt);
	}
	dput(mnt->mnt.mnt_root);
	deactivate_super(mnt->mnt.mnt_sb);
	mnt_free_id(mnt);
	call_rcu(&mnt->mnt_rcu, delayed_free_vfsmnt);
}

void mntput(struct vfsmount *mnt)
{
	struct mount *m;
	int count;

	if (!mnt)
		return;

	m = real_mount(mnt);
	rcu_read_lock();
	if (likely(READ_ONCE(m->mnt_ns))) {
		mnt_add_count(m, -1);
		rcu_read_unlock();
		return;
	}
	lock_mount_hash();

	smp_mb();
	mnt_add_count(m, -1);
	count = mnt_get_count(m);
	if (count != 0) {
		WARN_ON(count < 0);
		rcu_read_unlock();
		unlock_mount_hash();
		return;
	}
	if (unlikely(m->mnt.mnt_flags & MNT_DOOMED)) {
		rcu_read_unlock();
		unlock_mount_hash();
		return;
	}
	m->mnt.mnt_flags |= MNT_DOOMED;
	rcu_read_unlock();

	list_del(&m->mnt_instance);
	unlock_mount_hash();

	cleanup_mnt(m);
}

struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		mnt_add_count(real_mount(mnt), 1);
	return mnt;
}

static void namespace_unlock(void)
{
	up_write(&namespace_sem);
}

static inline void namespace_lock(void)
{
	down_write(&namespace_sem);
}

static void free_mnt_ns(struct mnt_namespace *);
static struct mnt_namespace *alloc_mnt_ns(struct user_namespace *, bool);

static int attach_recursive_mnt(struct mount *source_mnt,
				struct mount *dest_mnt,
				struct mountpoint *dest_mp, bool moving)
{
	struct mountpoint *smp;

	smp = get_mountpoint(source_mnt->mnt.mnt_root);
	if (IS_ERR(smp))
		return PTR_ERR(smp);

	lock_mount_hash();
	if (source_mnt->mnt_ns)
		list_del_init(&source_mnt->mnt_ns->list);
	mnt_set_mountpoint(dest_mnt, dest_mp, source_mnt);
	{
		struct mount *parent = source_mnt->mnt_parent;
		struct mount *m;
		LIST_HEAD(head);
		struct mnt_namespace *n = parent->mnt_ns;

		BUG_ON(parent == source_mnt);

		list_add_tail(&head, &source_mnt->mnt_list);
		list_for_each_entry(m, &head, mnt_list)
			m->mnt_ns = n;

		list_splice(&head, n->list.prev);

		n->mounts += n->pending_mounts;
		n->pending_mounts = 0;

		__attach_mnt(source_mnt, parent);
	}

	put_mountpoint(smp);
	unlock_mount_hash();

	return 0;
}

static struct mountpoint *lock_mount(struct path *path)
{
	struct vfsmount *mnt;
	struct dentry *dentry = path->dentry;
retry:
	down_write(&dentry->d_inode->i_rwsem); /* inode_lock inlined */
	if (unlikely(dentry->d_flags &
		     DCACHE_CANT_MOUNT)) { /* cant_mount inlined */
		inode_unlock(dentry->d_inode);
		return ERR_PTR(-ENOENT);
	}
	namespace_lock();
	mnt = lookup_mnt(path);
	if (likely(!mnt)) {
		struct mountpoint *mp = get_mountpoint(dentry);
		if (IS_ERR(mp)) {
			namespace_unlock();
			inode_unlock(dentry->d_inode);
			return mp;
		}
		return mp;
	}
	namespace_unlock();
	inode_unlock(path->dentry->d_inode);
	path_put(path);
	path->mnt = mnt;
	dentry = path->dentry = dget(mnt->mnt_root);
	goto retry;
}

int path_mount(const char *dev_name, struct path *path, const char *type_page,
	       unsigned long flags, void *data_page)
{
	unsigned int mnt_flags = 0, sb_flags;
	struct file_system_type *type;
	struct fs_context *fc;
	int err = 0;

	if ((flags & MS_MGC_MSK) == MS_MGC_VAL)
		flags &= ~MS_MGC_MSK;
	if (data_page)
		((char *)data_page)[PAGE_SIZE - 1] = 0;
	if (flags & MS_NOUSER)
		return -EINVAL;
	if (flags & (MS_REMOUNT | MS_BIND | MS_SHARED | MS_PRIVATE | MS_SLAVE |
		     MS_UNBINDABLE | MS_MOVE))
		return -EINVAL;

	if (!(flags & MS_NOATIME))
		mnt_flags |= MNT_RELATIME;
	if (flags & MS_RDONLY)
		mnt_flags |= MNT_READONLY;

	sb_flags = flags &
		   (SB_RDONLY | SB_SYNCHRONOUS | SB_MANDLOCK | SB_DIRSYNC |
		    SB_SILENT | SB_POSIXACL | SB_LAZYTIME | SB_I_VERSION);

	if (!type_page)
		return -EINVAL;
	type = get_fs_type(type_page);
	if (!type)
		return -ENODEV;

	fc = fs_context_for_mount(type, sb_flags);
	put_filesystem(type);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

	if (dev_name)
		err = vfs_parse_fs_string(fc, "source", dev_name,
					  strlen(dev_name));
	if (!err)
		err = parse_monolithic_mount_data(fc, data_page);
	if (!err)
		err = vfs_get_tree(fc);
	if (!err) {
		struct vfsmount *mnt;
		struct mountpoint *mp;

		up_write(&fc->root->d_sb->s_umount);
		mnt = vfs_create_mount(fc);
		if (IS_ERR(mnt)) {
			err = PTR_ERR(mnt);
		} else {
			mp = lock_mount(path);
			if (IS_ERR(mp)) {
				mntput(mnt);
				err = PTR_ERR(mp);
			} else {
				struct mount *newmnt = real_mount(mnt);
				struct mount *parent = real_mount(path->mnt);

				newmnt->mnt.mnt_flags = mnt_flags &
							~MNT_INTERNAL_FLAGS;
				err = attach_recursive_mnt(newmnt, parent, mp,
							   false);
				{
					struct dentry *dentry = mp->m_dentry;
					read_seqlock_excl(&mount_lock);
					put_mountpoint(mp);
					read_sequnlock_excl(&mount_lock);
					namespace_unlock();
					inode_unlock(dentry->d_inode);
				}
				if (err < 0)
					mntput(mnt);
			}
		}
	}

	put_fs_context(fc);
	return err;
}

static void dec_mnt_namespaces(struct ucounts *ucounts)
{
	dec_ucount(ucounts, UCOUNT_MNT_NAMESPACES);
}

static void free_mnt_ns(struct mnt_namespace *ns)
{
	if (!is_anon_ns(ns))
		ns_free_inum(&ns->ns);
	dec_mnt_namespaces(ns->ucounts);
	put_user_ns(ns->user_ns);
	kfree(ns);
}

static atomic64_t mnt_ns_seq = ATOMIC64_INIT(1);

static struct mnt_namespace *alloc_mnt_ns(struct user_namespace *user_ns,
					  bool anon)
{
	struct mnt_namespace *new_ns;
	struct ucounts *ucounts;

	ucounts = inc_ucount(user_ns, current_euid(), UCOUNT_MNT_NAMESPACES);
	if (!ucounts)
		return ERR_PTR(-ENOSPC);

	new_ns = kzalloc(sizeof(struct mnt_namespace), GFP_KERNEL_ACCOUNT);
	if (!new_ns) {
		dec_mnt_namespaces(ucounts);
		return ERR_PTR(-ENOMEM);
	}
	if (!anon)
		ns_alloc_inum(&new_ns->ns);
	if (!anon)
		new_ns->seq = atomic64_add_return(1, &mnt_ns_seq);
	refcount_set(&new_ns->ns.count, 1);
	INIT_LIST_HEAD(&new_ns->list);
	spin_lock_init(&new_ns->ns_lock);
	new_ns->user_ns = get_user_ns(user_ns);
	new_ns->ucounts = ucounts;
	return new_ns;
}

void __init mnt_init(void)
{
	int err;

	mnt_cache = kmem_cache_create(
		"mnt_cache", sizeof(struct mount), 0,
		SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT, NULL);

	mount_hashtable = alloc_large_system_hash(
		"Mount-cache", sizeof(struct hlist_head), mhash_entries, 19,
		HASH_ZERO, &m_hash_shift, &m_hash_mask, 0, 0);
	mountpoint_hashtable = alloc_large_system_hash(
		"Mountpoint-cache", sizeof(struct hlist_head), mphash_entries,
		19, HASH_ZERO, &mp_hash_shift, &mp_hash_mask, 0, 0);

	if (!mount_hashtable || !mountpoint_hashtable)
		panic("Failed to allocate mount hash table\n");

	err = sysfs_init();
	if (err)
		printk(KERN_WARNING "%s: sysfs_init error: %d\n", __func__,
		       err);
	shmem_init();
	{
		struct vfsmount *mnt;
		struct mount *m;
		struct mnt_namespace *ns;
		struct path root;

		mnt = vfs_kern_mount(&rootfs_fs_type, 0, "rootfs");
		if (IS_ERR(mnt))
			panic("Can't create rootfs");

		ns = alloc_mnt_ns(&init_user_ns, false);
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

void put_mnt_ns(struct mnt_namespace *ns)
{
	if (!refcount_dec_and_test(&ns->ns.count))
		return;
	free_mnt_ns(ns);
}

struct vfsmount *kern_mount(struct file_system_type *type)
{
	struct vfsmount *mnt;
	mnt = vfs_kern_mount(type, SB_KERNMOUNT, type->name);
	if (!IS_ERR(mnt)) {
		real_mount(mnt)->mnt_ns = MNT_NS_INTERNAL;
	}
	return mnt;
}
