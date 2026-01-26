
#include <linux/syscalls.h>
#include <linux/capability.h>
#include <linux/mnt_namespace.h>
#include <linux/user_namespace.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/cred.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/fs_struct.h>
#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/proc_ns.h>
#include <linux/memblock.h>
#include <linux/task_work.h>
#include <linux/sched/task.h>
#include <uapi/linux/mount.h>
#include <linux/fs_context.h>
#include <linux/shmem_fs.h>
#include <linux/mnt_idmapping.h>

/* --- 2026-01-26 01:10 --- Inlined from pnode.h */
#include "mount.h"
#define IS_MNT_SHARED(m) ((m)->mnt.mnt_flags & MNT_SHARED)
#define IS_MNT_LOCKED(m) ((m)->mnt.mnt_flags & MNT_LOCKED)
static inline void set_mnt_shared(struct mount *mnt)
{
	mnt->mnt.mnt_flags &= ~MNT_SHARED_MASK;
	mnt->mnt.mnt_flags |= MNT_SHARED;
}
int mnt_get_count(struct mount *mnt);
void mnt_set_mountpoint(struct mount *, struct mountpoint *, struct mount *);
/* end pnode.h */
#include "internal.h"

static unsigned int m_hash_mask __read_mostly;
static unsigned int m_hash_shift __read_mostly;
static unsigned int mp_hash_mask __read_mostly;
static unsigned int mp_hash_shift __read_mostly;

static __initdata unsigned long mhash_entries;

static __initdata unsigned long mphash_entries;

static u64 event;
static DEFINE_IDA(mnt_id_ida);
static DEFINE_IDA(mnt_group_ida);

static struct hlist_head *mount_hashtable __read_mostly;
static struct hlist_head *mountpoint_hashtable __read_mostly;
static struct kmem_cache *mnt_cache __read_mostly;
static DECLARE_RWSEM(namespace_sem);
static HLIST_HEAD(unmounted);
static LIST_HEAD(ex_mountpoints);

/* Removed: struct mount_kattr - never used */

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

/* mnt_alloc_group_id inlined into invent_group_ids */

static inline void mnt_add_count(struct mount *mnt, int n)
{
	preempt_disable();
	mnt->mnt_count += n;
	preempt_enable();
}

int mnt_get_count(struct mount *mnt)
{
	return mnt->mnt_count;
}

static struct mount *alloc_vfsmnt(const char *name)
{
	struct mount *mnt = kmem_cache_zalloc(mnt_cache, GFP_KERNEL);
	if (mnt) {
		int res;

		/* ida_alloc inlined */
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

/* mnt_is_readonly inlined - only called once */

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
	if ((m->mnt_flags & MNT_READONLY) || sb_rdonly(m->mnt_sb)) {
		mnt_dec_writers(mnt);
		ret = -EROFS;
	}
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

void __mnt_drop_write(struct vfsmount *mnt)
{
	preempt_disable();
	mnt_dec_writers(real_mount(mnt));
	preempt_enable();
}

void mnt_drop_write(struct vfsmount *mnt)
{
	__mnt_drop_write(mnt);
	sb_end_write(mnt->mnt_sb);
}

/* free_vfsmnt removed - inlined into delayed_free_vfsmnt (~9 LOC) */

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

struct vfsmount *lookup_mnt(const struct path *path)
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

static inline int check_mnt(struct mount *mnt)
{
	return mnt->mnt_ns == current->nsproxy->mnt_ns;
}

static void touch_mnt_namespace(struct mnt_namespace *ns)
{
	if (ns) {
		ns->event = ++event;
		wake_up_interruptible(&ns->poll);
	}
}

static struct mountpoint *unhash_mnt(struct mount *mnt)
{
	struct mountpoint *mp;
	mnt->mnt_parent = mnt;
	mnt->mnt_mountpoint = mnt->mnt.mnt_root;
	list_del_init(&mnt->mnt_child);
	hlist_del_init_rcu(&mnt->mnt_hash);
	hlist_del_init(&mnt->mnt_mp_list);
	mp = mnt->mnt_mp;
	mnt->mnt_mp = NULL;
	return mp;
}

/* umount_mnt inlined into do_umount - put_mountpoint(unhash_mnt(mnt)) */

void mnt_set_mountpoint(struct mount *mnt, struct mountpoint *mp,
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

/* attach_mnt inlined into do_move_mount - called only once */

/* commit_tree inlined into graft_tree */

static struct mount *next_mnt(struct mount *p, struct mount *root)
{
	struct list_head *next = p->mnt_mounts.next;
	if (next == &p->mnt_mounts) {
		while (1) {
			if (p == root)
				return NULL;
			next = p->mnt_child.next;
			if (next != &p->mnt_parent->mnt_mounts)
				break;
			p = p->mnt_parent;
		}
	}
	return list_entry(next, struct mount, mnt_child);
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

struct vfsmount *fc_mount(struct fs_context *fc)
{
	int err = vfs_get_tree(fc);
	if (!err) {
		up_write(&fc->root->d_sb->s_umount);
		return vfs_create_mount(fc);
	}
	return ERR_PTR(err);
}

struct vfsmount *vfs_kern_mount(struct file_system_type *type, int flags,
				const char *name, void *data)
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
		ret = parse_monolithic_mount_data(fc, data);
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
	if (unlikely(mnt->mnt_pins.first))
		mnt_pin_kill(mnt);
	hlist_for_each_entry_safe(m, p, &mnt->mnt_stuck_children, mnt_umount) {
		hlist_del(&m->mnt_umount);
		mntput(&m->mnt);
	}
	dput(mnt->mnt.mnt_root);
	deactivate_super(mnt->mnt.mnt_sb);
	mnt_free_id(mnt);
	call_rcu(&mnt->mnt_rcu, delayed_free_vfsmnt);
}

static void __cleanup_mnt(struct rcu_head *head)
{
	cleanup_mnt(container_of(head, struct mount, mnt_rcu));
}

static LLIST_HEAD(delayed_mntput_list);
static void delayed_mntput(struct work_struct *unused)
{
	struct llist_node *node = llist_del_all(&delayed_mntput_list);
	struct mount *m, *t;

	llist_for_each_entry_safe(m, t, node, mnt_llist)
		cleanup_mnt(m);
}
static DECLARE_DELAYED_WORK(delayed_mntput_work, delayed_mntput);

static void mntput_no_expire(struct mount *mnt)
{
	LIST_HEAD(list);
	int count;

	rcu_read_lock();
	if (likely(READ_ONCE(mnt->mnt_ns))) {
		mnt_add_count(mnt, -1);
		rcu_read_unlock();
		return;
	}
	lock_mount_hash();

	smp_mb();
	mnt_add_count(mnt, -1);
	count = mnt_get_count(mnt);
	if (count != 0) {
		WARN_ON(count < 0);
		rcu_read_unlock();
		unlock_mount_hash();
		return;
	}
	if (unlikely(mnt->mnt.mnt_flags & MNT_DOOMED)) {
		rcu_read_unlock();
		unlock_mount_hash();
		return;
	}
	mnt->mnt.mnt_flags |= MNT_DOOMED;
	rcu_read_unlock();

	list_del(&mnt->mnt_instance);

	if (unlikely(!list_empty(&mnt->mnt_mounts))) {
		struct mount *p, *tmp;
		list_for_each_entry_safe(p, tmp, &mnt->mnt_mounts, mnt_child) {
			__put_mountpoint(unhash_mnt(p), &list);
			hlist_add_head(&p->mnt_umount,
				       &mnt->mnt_stuck_children);
		}
	}
	unlock_mount_hash();
	shrink_dentry_list(&list);

	if (likely(!(mnt->mnt.mnt_flags & MNT_INTERNAL))) {
		struct task_struct *task = current;
		if (likely(!(task->flags & PF_KTHREAD))) {
			init_task_work(&mnt->mnt_rcu, __cleanup_mnt);
			if (!task_work_add(task, &mnt->mnt_rcu, TWA_RESUME))
				return;
		}
		if (llist_add(&mnt->mnt_llist, &delayed_mntput_list))
			schedule_delayed_work(&delayed_mntput_work, 1);
		return;
	}
	cleanup_mnt(mnt);
}

void mntput(struct vfsmount *mnt)
{
	if (mnt) {
		struct mount *m = real_mount(mnt);

		if (unlikely(m->mnt_expiry_mark))
			m->mnt_expiry_mark = 0;
		mntput_no_expire(m);
	}
}

struct vfsmount *mntget(struct vfsmount *mnt)
{
	if (mnt)
		mnt_add_count(real_mount(mnt), 1);
	return mnt;
}

static void namespace_unlock(void)
{
	struct hlist_head head;
	struct hlist_node *p;
	struct mount *m;
	LIST_HEAD(list);

	/* hlist_move_list inlined */
	head.first = unmounted.first;
	if (head.first)
		head.first->pprev = &head.first;
	unmounted.first = NULL;
	/* list_splice_init inlined */
	if (!list_empty(&ex_mountpoints)) {
		__list_splice(&ex_mountpoints, &list, list.next);
		INIT_LIST_HEAD(&ex_mountpoints);
	}

	up_write(&namespace_sem);

	shrink_dentry_list(&list);

	if (likely(hlist_empty(&head)))
		return;
	/* synchronize_rcu_expedited call removed - empty stub (single task) */
	hlist_for_each_entry_safe(m, p, &head, mnt_umount) {
		hlist_del(&m->mnt_umount);
		mntput(&m->mnt);
	}
}

static inline void namespace_lock(void)
{
	down_write(&namespace_sem);
}

enum umount_tree_flags {
	UMOUNT_SYNC = 1,
	UMOUNT_PROPAGATE = 2,
	UMOUNT_CONNECTED = 4,
};

/* disconnect_mount inlined into umount_tree */

static void umount_tree(struct mount *mnt, enum umount_tree_flags how)
{
	LIST_HEAD(tmp_list);
	struct mount *p;

	for (p = mnt; p; p = next_mnt(p, mnt)) {
		p->mnt.mnt_flags |= MNT_UMOUNT;
		list_move(&p->mnt_list, &tmp_list);
	}

	list_for_each_entry(p, &tmp_list, mnt_list) {
		list_del_init(&p->mnt_child);
	}

	/* propagate_umount call removed - always returns 0, no side effects */

	while (!list_empty(&tmp_list)) {
		struct mnt_namespace *ns;
		bool disconnect;
		p = list_first_entry(&tmp_list, struct mount, mnt_list);
		list_del_init(&p->mnt_expire);
		list_del_init(&p->mnt_list);
		ns = p->mnt_ns;
		if (ns) {
			ns->mounts--;
			if (ns->event != event) {
				ns->event = event;
				wake_up_interruptible(&ns->poll);
			}
		}
		p->mnt_ns = NULL;
		if (how & UMOUNT_SYNC)
			p->mnt.mnt_flags |= MNT_SYNC_UMOUNT;

		/* disconnect_mount inlined */
		disconnect = (how & UMOUNT_SYNC) || !mnt_has_parent(p) ||
			     !(p->mnt_parent->mnt.mnt_flags & MNT_UMOUNT) ||
			     (!(how & UMOUNT_CONNECTED) && !IS_MNT_LOCKED(p));
		if (mnt_has_parent(p)) {
			mnt_add_count(p->mnt_parent, -1);
			if (!disconnect) {
				list_add_tail(&p->mnt_child,
					      &p->mnt_parent->mnt_mounts);
			} else {
				put_mountpoint(
					unhash_mnt(p)); /* inlined umount_mnt */
			}
		}
		if (disconnect)
			hlist_add_head(&p->mnt_umount, &unmounted);
	}
}

/* do_umount, can_umount, path_umount removed - cascading removal after path_umount's only caller removed */

/* __detach_mounts removed - empty stub moved inline to mount.h */
/* may_mount removed - never called (~4 LOC) */
/* to_mnt_ns removed - no callers after mntns_* cleanup */

static void free_mnt_ns(struct mnt_namespace *);
static struct mnt_namespace *alloc_mnt_ns(struct user_namespace *, bool);

void dissolve_on_fput(struct vfsmount *mnt)
{
	struct mnt_namespace *ns;
	namespace_lock();
	lock_mount_hash();
	ns = real_mount(mnt)->mnt_ns;
	if (ns) {
		if (is_anon_ns(ns))
			umount_tree(real_mount(mnt), UMOUNT_CONNECTED);
		else
			ns = NULL;
	}
	unlock_mount_hash();
	namespace_unlock();
	if (ns)
		free_mnt_ns(ns);
}

/* lock_mnt_tree removed - only caller was in dead tree_list loop */

static int invent_group_ids(struct mount *mnt, bool recurse)
{
	struct mount *p;

	for (p = mnt; p; p = recurse ? next_mnt(p, mnt) : NULL) {
		if (!p->mnt_group_id && !IS_MNT_SHARED(p)) {
			/* mnt_alloc_group_id inlined, ida_alloc_min inlined */
			int res = ida_alloc_range(&mnt_group_ida, 1, ~0,
						  GFP_KERNEL);
			if (res < 0)
				return res;
			p->mnt_group_id = res;
		}
	}

	return 0;
}

static int attach_recursive_mnt(struct mount *source_mnt,
				struct mount *dest_mnt,
				struct mountpoint *dest_mp, bool moving)
{
	struct mnt_namespace *ns = dest_mnt->mnt_ns;
	struct mountpoint *smp;
	struct mount *p;
	int err;

	smp = get_mountpoint(source_mnt->mnt.mnt_root);
	if (IS_ERR(smp))
		return PTR_ERR(smp);

	/* count_mounts call removed - always returned 0 */

	if (IS_MNT_SHARED(dest_mnt)) {
		err = invent_group_ids(source_mnt, true);
		if (err)
			goto out;
		/* propagate_mnt call removed - always returns 0 */
		lock_mount_hash();
		for (p = source_mnt; p; p = next_mnt(p, source_mnt))
			set_mnt_shared(p);
	} else {
		lock_mount_hash();
	}
	if (moving) {
		unhash_mnt(source_mnt);
		/* attach_mnt inlined */
		mnt_set_mountpoint(dest_mnt, dest_mp, source_mnt);
		__attach_mnt(source_mnt, dest_mnt);
		touch_mnt_namespace(source_mnt->mnt_ns);
	} else {
		if (source_mnt->mnt_ns) {
			list_del_init(&source_mnt->mnt_ns->list);
		}
		mnt_set_mountpoint(dest_mnt, dest_mp, source_mnt);
		/* Inlined commit_tree */
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
			touch_mnt_namespace(n);
		}
	}

	put_mountpoint(smp);
	unlock_mount_hash();

	return 0;

out:
	/* out_cleanup_ids unreachable now (propagate_mnt always returned 0) */
	ns->pending_mounts = 0;

	read_seqlock_excl(&mount_lock);
	put_mountpoint(smp);
	read_sequnlock_excl(&mount_lock);

	return err;
}

static struct mountpoint *lock_mount(struct path *path)
{
	struct vfsmount *mnt;
	struct dentry *dentry = path->dentry;
retry:
	inode_lock(dentry->d_inode);
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

/* unlock_mount inlined into do_loopback */
/* graft_tree inlined at call site - only called once */

/* flags_to_propagation_type inlined - only called once */

static int do_change_type(struct path *path, int ms_flags)
{
	struct mount *mnt = real_mount(path->mnt);
	int recurse = ms_flags & MS_REC;
	int type = ms_flags & ~(MS_REC | MS_SILENT);
	int err = 0;

	if (path->dentry != path->mnt->mnt_root)
		return -EINVAL;

	/* Inline: flags_to_propagation_type logic */
	if ((type & ~(MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE)) ||
	    !is_power_of_2(type))
		return -EINVAL;

	namespace_lock();
	if (type == MS_SHARED) {
		err = invent_group_ids(mnt, recurse);
		if (err)
			goto out_unlock;
	}

	lock_mount_hash();
	unlock_mount_hash();

out_unlock:
	namespace_unlock();
	return err;
}

static int do_add_mount(struct mount *newmnt, struct mountpoint *mp,
			const struct path *path, int mnt_flags)
{
	struct mount *parent = real_mount(path->mnt);

	mnt_flags &= ~MNT_INTERNAL_FLAGS;

	if (unlikely(!check_mnt(parent))) {
		if (!(mnt_flags & MNT_SHRINKABLE))
			return -EINVAL;

		if (!parent->mnt_ns)
			return -EINVAL;
	}

	if (path->mnt->mnt_sb == newmnt->mnt.mnt_sb &&
	    path->mnt->mnt_root == path->dentry)
		return -EBUSY;

	/* d_is_symlink check removed - always false, no symlinks created */
	newmnt->mnt.mnt_flags = mnt_flags;

	/* Inline: graft_tree logic */
	if (newmnt->mnt.mnt_sb->s_flags & SB_NOUSER)
		return -EINVAL;
	if (d_can_lookup(mp->m_dentry) != d_can_lookup(newmnt->mnt.mnt_root))
		return -ENOTDIR;
	return attach_recursive_mnt(newmnt, parent, mp, false);
}

static bool mount_too_revealing(const struct super_block *sb,
				int *new_mnt_flags);

/* do_new_mount_fc inlined into do_new_mount */

static int do_new_mount(struct path *path, const char *fstype, int sb_flags,
			int mnt_flags, const char *name, void *data)
{
	struct file_system_type *type;
	struct fs_context *fc;
	int err = 0;

	if (!fstype)
		return -EINVAL;

	type = get_fs_type(fstype);
	if (!type)
		return -ENODEV;

	fc = fs_context_for_mount(type, sb_flags);
	put_filesystem(type);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

	if (name)
		err = vfs_parse_fs_string(fc, "source", name, strlen(name));
	if (!err)
		err = parse_monolithic_mount_data(fc, data);
	/* mount_capable() always returns true - check removed */
	if (!err)
		err = vfs_get_tree(fc);
	/* Inlined do_new_mount_fc */
	if (!err) {
		struct vfsmount *mnt;
		struct mountpoint *mp;
		struct super_block *sb = fc->root->d_sb;

		if (mount_too_revealing(sb, &mnt_flags)) {
			fc_drop_locked(fc);
			err = -EPERM;
		} else {
			up_write(&sb->s_umount);

			mnt = vfs_create_mount(fc);
			if (IS_ERR(mnt)) {
				err = PTR_ERR(mnt);
			} else {
				mp = lock_mount(path);
				if (IS_ERR(mp)) {
					mntput(mnt);
					err = PTR_ERR(mp);
				} else {
					err = do_add_mount(real_mount(mnt), mp,
							   path, mnt_flags);
					{
						struct dentry *dentry =
							mp->m_dentry;
						read_seqlock_excl(&mount_lock);
						put_mountpoint(mp);
						read_sequnlock_excl(
							&mount_lock);
						namespace_unlock();
						inode_unlock(dentry->d_inode);
					}
					if (err < 0)
						mntput(mnt);
				}
			}
		}
	}

	put_fs_context(fc);
	return err;
}

/* Removed: copy_mount_options, copy_mount_string - mount syscall stubbed */

int path_mount(const char *dev_name, struct path *path, const char *type_page,
	       unsigned long flags, void *data_page)
{
	unsigned int mnt_flags = 0, sb_flags;
	/* ret removed - never used after security/may_mount simplifications */

	if ((flags & MS_MGC_MSK) == MS_MGC_VAL)
		flags &= ~MS_MGC_MSK;

	if (data_page)
		((char *)data_page)[PAGE_SIZE - 1] = 0;

	if (flags & MS_NOUSER)
		return -EINVAL;

	if (!(flags & MS_NOATIME))
		mnt_flags |= MNT_RELATIME;

	if (flags & MS_NOSUID)
		mnt_flags |= MNT_NOSUID;
	if (flags & MS_NODEV)
		mnt_flags |= MNT_NODEV;
	if (flags & MS_NOEXEC)
		mnt_flags |= MNT_NOEXEC;
	if (flags & MS_NOATIME)
		mnt_flags |= MNT_NOATIME;
	if (flags & MS_NODIRATIME)
		mnt_flags |= MNT_NODIRATIME;
	if (flags & MS_STRICTATIME)
		mnt_flags &= ~(MNT_RELATIME | MNT_NOATIME);
	if (flags & MS_RDONLY)
		mnt_flags |= MNT_READONLY;
	if (flags & MS_NOSYMFOLLOW)
		mnt_flags |= MNT_NOSYMFOLLOW;

	if ((flags & MS_REMOUNT) &&
	    ((flags & (MS_NOATIME | MS_NODIRATIME | MS_RELATIME |
		       MS_STRICTATIME)) == 0)) {
		mnt_flags &= ~MNT_ATIME_MASK;
		mnt_flags |= path->mnt->mnt_flags & MNT_ATIME_MASK;
	}

	sb_flags = flags &
		   (SB_RDONLY | SB_SYNCHRONOUS | SB_MANDLOCK | SB_DIRSYNC |
		    SB_SILENT | SB_POSIXACL | SB_LAZYTIME | SB_I_VERSION);

	if ((flags & (MS_REMOUNT | MS_BIND)) == (MS_REMOUNT | MS_BIND))
		return -ENOSYS;
	if (flags & MS_REMOUNT)
		return -ENOSYS;
	if (flags & MS_BIND)
		return -EINVAL;
	if (flags & (MS_SHARED | MS_PRIVATE | MS_SLAVE | MS_UNBINDABLE))
		return do_change_type(path, flags);
	if (flags & MS_MOVE)
		return -EINVAL;

	return do_new_mount(path, type_page, sb_flags, mnt_flags, dev_name,
			    data_page);
}

/* inc_mnt_namespaces inlined - single caller */

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
	/* ns.ops assignment removed - never read */
	if (!anon)
		new_ns->seq = atomic64_add_return(1, &mnt_ns_seq);
	refcount_set(&new_ns->ns.count, 1);
	INIT_LIST_HEAD(&new_ns->list);
	init_waitqueue_head(&new_ns->poll);
	spin_lock_init(&new_ns->ns_lock);
	new_ns->user_ns = get_user_ns(user_ns);
	new_ns->ucounts = ucounts;
	return new_ns;
}

/* copy_mnt_ns removed - create_new_namespaces no longer called */

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
	/* Stub: fs_kobj not used in minimal kernel */
	shmem_init();
	/* Inlined init_mount_tree */
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
	mnt = vfs_kern_mount(type, SB_KERNMOUNT, type->name, NULL);
	if (!IS_ERR(mnt)) {
		real_mount(mnt)->mnt_ns = MNT_NS_INTERNAL;
	}
	return mnt;
}

static bool mnt_already_visible(struct mnt_namespace *ns,
				const struct super_block *sb,
				int *new_mnt_flags)
{
	int new_flags = *new_mnt_flags;
	struct mount *mnt;
	bool visible = false;

	down_read(&namespace_sem);
	spin_lock(&ns->ns_lock);
	list_for_each_entry(mnt, &ns->list, mnt_list) {
		struct mount *child;
		int mnt_flags;

		if (mnt->mnt.mnt_flags & MNT_CURSOR)
			continue;

		if (mnt->mnt.mnt_sb->s_type != sb->s_type)
			continue;

		if (mnt->mnt.mnt_root != mnt->mnt.mnt_sb->s_root)
			continue;

		mnt_flags = mnt->mnt.mnt_flags;

		if (sb_rdonly(mnt->mnt.mnt_sb))
			mnt_flags |= MNT_LOCK_READONLY;

		if ((mnt_flags & MNT_LOCK_READONLY) &&
		    !(new_flags & MNT_READONLY))
			continue;
		if ((mnt_flags & MNT_LOCK_ATIME) &&
		    ((mnt_flags & MNT_ATIME_MASK) !=
		     (new_flags & MNT_ATIME_MASK)))
			continue;

		list_for_each_entry(child, &mnt->mnt_mounts, mnt_child) {
			struct inode *inode = child->mnt_mountpoint->d_inode;

			if (!(child->mnt.mnt_flags & MNT_LOCKED))
				continue;

			if (!is_empty_dir_inode(inode))
				goto next;
		}

		*new_mnt_flags |= mnt_flags &
				  (MNT_LOCK_READONLY | MNT_LOCK_ATIME);
		visible = true;
		goto found;
next:;
	}
found:
	spin_unlock(&ns->ns_lock);
	up_read(&namespace_sem);
	return visible;
}

static bool mount_too_revealing(const struct super_block *sb,
				int *new_mnt_flags)
{
	const unsigned long required_iflags = SB_I_NOEXEC | SB_I_NODEV;
	struct mnt_namespace *ns = current->nsproxy->mnt_ns;
	unsigned long s_iflags;

	if (ns->user_ns == &init_user_ns)
		return false;

	s_iflags = sb->s_iflags;
	if (!(s_iflags & SB_I_USERNS_VISIBLE))
		return false;

	if ((s_iflags & required_iflags) != required_iflags) {
		WARN_ONCE(1, "Expected s_iflags to contain 0x%lx\n",
			  required_iflags);
		return true;
	}

	return !mnt_already_visible(ns, sb, new_mnt_flags);
}

bool mnt_may_suid(struct vfsmount *mnt)
{
	return !(mnt->mnt_flags & MNT_NOSUID) && check_mnt(real_mount(mnt)) &&
	       current_in_userns(mnt->mnt_sb->s_user_ns);
}

/* mntns_get, mntns_put, mntns_install, mntns_owner, mntns_operations removed - ns.ops never read */
