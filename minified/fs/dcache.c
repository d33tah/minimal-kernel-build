#include <linux/fs.h>
#include <linux/memblock.h>
#include "internal.h"

__cacheline_aligned_in_smp DEFINE_SEQLOCK(rename_lock);

static struct kmem_cache *dentry_cache __read_mostly;

static const struct qstr slash_name = QSTR_INIT("/", 1);

static unsigned int d_hash_shift __read_mostly;

static struct hlist_bl_head *dentry_hashtable __read_mostly;

static inline struct hlist_bl_head *d_hash(unsigned int hash)
{
	return dentry_hashtable + (hash >> d_hash_shift);
}

#include <asm/word-at-a-time.h>

static inline int dentry_cmp(const struct dentry *dentry,
			     const unsigned char *ct, unsigned tcount)
{
	const unsigned char *cs = READ_ONCE(dentry->d_name.name);
	unsigned long a, b, mask;

	for (;;) {
		a = read_word_at_a_time(cs);
		b = load_unaligned_zeropad(ct);
		if (tcount < sizeof(unsigned long))
			break;
		if (unlikely(a != b))
			return 1;
		cs += sizeof(unsigned long);
		ct += sizeof(unsigned long);
		tcount -= sizeof(unsigned long);
		if (!tcount)
			return 0;
	}
	mask = bytemask_from_count(tcount);
	return unlikely(!!((a ^ b) & mask));
}

static void __d_free(struct rcu_head *head)
{
	struct dentry *dentry = container_of(head, struct dentry, d_u.d_rcu);
	kmem_cache_free(dentry_cache, dentry);
}

static inline void __d_set_inode_and_type(struct dentry *dentry,
					  struct inode *inode,
					  unsigned type_flags)
{
	unsigned flags;

	dentry->d_inode = inode;
	flags = READ_ONCE(dentry->d_flags);
	flags &= ~(DCACHE_ENTRY_TYPE | DCACHE_FALLTHRU);
	flags |= type_flags;
	smp_store_release(&dentry->d_flags, flags);
}

static void dentry_unlink_inode(struct dentry *dentry)
	__releases(dentry->d_lock) __releases(dentry->d_inode->i_lock)
{
	struct inode *inode = dentry->d_inode;
	unsigned flags;

	raw_write_seqcount_begin(&dentry->d_seq);
	flags = READ_ONCE(dentry->d_flags);
	flags &= ~(DCACHE_ENTRY_TYPE | DCACHE_FALLTHRU);
	WRITE_ONCE(dentry->d_flags, flags);
	dentry->d_inode = NULL;
	hlist_del_init(&dentry->d_u.d_alias);
	raw_write_seqcount_end(&dentry->d_seq);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&inode->i_lock);
	iput(inode);
}

static void __d_drop(struct dentry *dentry)
{
	if (!d_unhashed(dentry)) {
		struct hlist_bl_head *b;

		if (unlikely(IS_ROOT(dentry)))
			b = &dentry->d_sb->s_roots;
		else
			b = d_hash(dentry->d_name.hash);

		hlist_bl_lock(b);
		__hlist_bl_del(&dentry->d_hash);
		hlist_bl_unlock(b);
		dentry->d_hash.pprev = NULL;
		write_seqcount_invalidate(&dentry->d_seq);
	}
}

static void __dentry_kill(struct dentry *dentry)
{
	struct dentry *parent = NULL;
	if (!IS_ROOT(dentry))
		parent = dentry->d_parent;

	lockref_mark_dead(&dentry->d_lockref);

	__d_drop(dentry);
	{
		dentry->d_flags |= DCACHE_DENTRY_KILLED;
		if (!unlikely(list_empty(&dentry->d_child)))
			__list_del_entry(&dentry->d_child);
	}
	if (parent)
		spin_unlock(&parent->d_lock);
	if (dentry->d_inode)
		dentry_unlink_inode(dentry);
	else
		spin_unlock(&dentry->d_lock);

	WARN_ON(!hlist_unhashed(&dentry->d_u.d_alias));
	call_rcu(&dentry->d_u.d_rcu, __d_free);
	cond_resched();
}

static struct dentry *__lock_parent(struct dentry *dentry)
{
	struct dentry *parent;
	rcu_read_lock();
	spin_unlock(&dentry->d_lock);
again:
	parent = READ_ONCE(dentry->d_parent);
	spin_lock(&parent->d_lock);

	if (unlikely(parent != dentry->d_parent)) {
		spin_unlock(&parent->d_lock);
		goto again;
	}
	rcu_read_unlock();
	if (parent != dentry)
		spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	else
		parent = NULL;
	return parent;
}

static inline bool retain_dentry(struct dentry *dentry)
{
	if (unlikely(d_unhashed(dentry)))
		return false;
	dentry->d_lockref.count--;
	return true;
}

static struct dentry *dentry_kill(struct dentry *dentry)
	__releases(dentry->d_lock)
{
	struct inode *inode = dentry->d_inode;
	struct dentry *parent = NULL;

	if (inode && unlikely(!spin_trylock(&inode->i_lock)))
		goto slow_positive;

	if (!IS_ROOT(dentry)) {
		parent = dentry->d_parent;
		if (unlikely(!spin_trylock(&parent->d_lock))) {
			parent = __lock_parent(dentry);
			if (likely(inode || !dentry->d_inode))
				goto got_locks;

			if (parent)
				spin_unlock(&parent->d_lock);
			inode = dentry->d_inode;
			goto slow_positive;
		}
	}
	__dentry_kill(dentry);
	return parent;

slow_positive:
	spin_unlock(&dentry->d_lock);
	spin_lock(&inode->i_lock);
	spin_lock(&dentry->d_lock);
	{
		struct dentry *p = dentry->d_parent;
		parent = IS_ROOT(dentry) ? NULL :
			 likely(spin_trylock(&p->d_lock)) ?
					   p :
					   __lock_parent(dentry);
	}
got_locks:
	if (unlikely(dentry->d_lockref.count != 1)) {
		dentry->d_lockref.count--;
	} else if (likely(!retain_dentry(dentry))) {
		__dentry_kill(dentry);
		return parent;
	}

	if (inode)
		spin_unlock(&inode->i_lock);
	if (parent)
		spin_unlock(&parent->d_lock);
	spin_unlock(&dentry->d_lock);
	return NULL;
}

static inline bool fast_dput(struct dentry *dentry)
{
	int ret = lockref_put_return(&dentry->d_lockref);

	if (unlikely(ret < 0)) {
		spin_lock(&dentry->d_lock);
		if (dentry->d_lockref.count > 1) {
			dentry->d_lockref.count--;
			spin_unlock(&dentry->d_lock);
			return true;
		}
		return false;
	}
	return ret != 0;
}

void dput(struct dentry *dentry)
{
	while (dentry) {
		rcu_read_lock();
		if (likely(fast_dput(dentry))) {
			rcu_read_unlock();
			return;
		}

		rcu_read_unlock();

		if (likely(retain_dentry(dentry))) {
			spin_unlock(&dentry->d_lock);
			return;
		}

		dentry = dentry_kill(dentry);
	}
}

void dput_to_list(struct dentry *dentry, struct list_head *list)
{
	rcu_read_lock();
	if (likely(fast_dput(dentry))) {
		rcu_read_unlock();
		return;
	}
	rcu_read_unlock();
	if (!retain_dentry(dentry)) {
		if (!--dentry->d_lockref.count) {
			list_add(&dentry->d_lru, list);
			dentry->d_flags |= DCACHE_SHRINK_LIST | DCACHE_LRU_LIST;
		}
	}
	spin_unlock(&dentry->d_lock);
}

int d_set_mounted(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	if (!d_unlinked(dentry) && !d_mountpoint(dentry)) {
		dentry->d_flags |= DCACHE_MOUNTED;
		spin_unlock(&dentry->d_lock);
		return 0;
	}
	spin_unlock(&dentry->d_lock);
	return -EBUSY;
}

static void do_one_tree(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
	dput(dentry);
}

void shrink_dcache_for_umount(struct super_block *sb)
{
	struct dentry *dentry;

	WARN(down_read_trylock(&sb->s_umount),
	     "s_umount should've been locked");

	dentry = sb->s_root;
	sb->s_root = NULL;
	do_one_tree(dentry);

	while (!hlist_bl_empty(&sb->s_roots)) {
		dentry = dget(hlist_bl_entry(hlist_bl_first(&sb->s_roots),
					     struct dentry, d_hash));
		do_one_tree(dentry);
	}
}

static struct dentry *__d_alloc(struct super_block *sb, const struct qstr *name)
{
	struct dentry *dentry;
	char *dname;

	dentry = kmem_cache_alloc_lru(dentry_cache, &sb->s_dentry_lru,
				      GFP_KERNEL);
	if (!dentry)
		return NULL;

	dentry->d_iname[DNAME_INLINE_LEN - 1] = 0;
	if (unlikely(!name)) {
		name = &slash_name;
	}
	dname = dentry->d_iname;

	dentry->d_name.len = name->len;
	dentry->d_name.hash = name->hash;
	memcpy(dname, name->name, name->len);
	dname[name->len] = 0;

	smp_store_release(&dentry->d_name.name, dname);

	dentry->d_lockref.count = 1;
	dentry->d_flags = 0;
	spin_lock_init(&dentry->d_lock);
	seqcount_spinlock_init(&dentry->d_seq, &dentry->d_lock);
	dentry->d_inode = NULL;
	dentry->d_parent = dentry;
	dentry->d_sb = sb;
	INIT_HLIST_BL_NODE(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_lru);
	INIT_LIST_HEAD(&dentry->d_subdirs);
	INIT_HLIST_NODE(&dentry->d_u.d_alias);
	INIT_LIST_HEAD(&dentry->d_child);
	return dentry;
}

static struct dentry *d_alloc(struct dentry *parent, const struct qstr *name)
{
	struct dentry *dentry = __d_alloc(parent->d_sb, name);
	if (!dentry)
		return NULL;
	spin_lock(&parent->d_lock);

	parent->d_lockref.count++;
	dentry->d_parent = parent;
	list_add(&dentry->d_child, &parent->d_subdirs);
	spin_unlock(&parent->d_lock);

	return dentry;
}

static unsigned d_flags_for_inode(struct inode *inode)
{
	unsigned add_flags = DCACHE_REGULAR_TYPE;

	if (!inode)
		return DCACHE_MISS_TYPE;

	if (S_ISDIR(inode->i_mode)) {
		add_flags = DCACHE_DIRECTORY_TYPE;
		if (unlikely(!(inode->i_opflags & IOP_LOOKUP))) {
			if (unlikely(!inode->i_op->lookup))
				add_flags = DCACHE_AUTODIR_TYPE;
			else
				inode->i_opflags |= IOP_LOOKUP;
		}
		goto type_determined;
	}

	inode->i_opflags |= IOP_NOFOLLOW;

	if (unlikely(!S_ISREG(inode->i_mode)))
		add_flags = DCACHE_SPECIAL_TYPE;

type_determined:
	if (unlikely(IS_AUTOMOUNT(inode)))
		add_flags |= DCACHE_NEED_AUTOMOUNT;
	return add_flags;
}

void d_instantiate(struct dentry *entry, struct inode *inode)
{
	BUG_ON(!hlist_unhashed(&entry->d_u.d_alias));
	if (inode) {
		unsigned add_flags = d_flags_for_inode(inode);
		WARN_ON(d_in_lookup(entry));

		spin_lock(&inode->i_lock);
		spin_lock(&entry->d_lock);
		hlist_add_head(&entry->d_u.d_alias, &inode->i_dentry);
		raw_write_seqcount_begin(&entry->d_seq);
		__d_set_inode_and_type(entry, inode, add_flags);
		raw_write_seqcount_end(&entry->d_seq);
		spin_unlock(&entry->d_lock);
		spin_unlock(&inode->i_lock);
	}
}

struct dentry *d_make_root(struct inode *root_inode)
{
	struct dentry *res = NULL;

	if (root_inode) {
		res = __d_alloc(root_inode->i_sb,
				NULL); /* inlined d_alloc_anon */
		if (res)
			d_instantiate(res, root_inode);
		else
			iput(root_inode);
	}
	return res;
}

static inline bool d_same_name(const struct dentry *dentry,
			       const struct dentry *parent,
			       const struct qstr *name)
{
	if (dentry->d_name.len != name->len)
		return false;
	return dentry_cmp(dentry, name->name, name->len) == 0;
}

struct dentry *__d_lookup_rcu(const struct dentry *parent,
			      const struct qstr *name, unsigned *seqp)
{
	u64 hashlen = name->hash_len;
	const unsigned char *str = name->name;
	struct hlist_bl_head *b = d_hash(hashlen_hash(hashlen));
	struct hlist_bl_node *node;
	struct dentry *dentry;

	hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash) {
		unsigned seq;
		seq = raw_seqcount_begin(&dentry->d_seq);
		if (dentry->d_parent != parent)
			continue;
		if (d_unhashed(dentry))
			continue;

		if (dentry->d_name.hash_len != hashlen)
			continue;
		if (dentry_cmp(dentry, str, hashlen_len(hashlen)) != 0)
			continue;
		*seqp = seq;
		return dentry;
	}
	return NULL;
}

struct dentry *d_lookup(const struct dentry *parent, const struct qstr *name)
{
	struct dentry *dentry;
	unsigned seq;

	do {
		seq = read_seqbegin(&rename_lock);
		dentry = __d_lookup(parent, name);
		if (dentry)
			break;
	} while (read_seqretry(&rename_lock, seq));
	return dentry;
}

struct dentry *__d_lookup(const struct dentry *parent, const struct qstr *name)
{
	unsigned int hash = name->hash;
	struct hlist_bl_head *b = d_hash(hash);
	struct hlist_bl_node *node;
	struct dentry *found = NULL;
	struct dentry *dentry;

	rcu_read_lock();

	hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash) {
		if (dentry->d_name.hash != hash)
			continue;

		spin_lock(&dentry->d_lock);
		if (dentry->d_parent != parent)
			goto next;
		if (d_unhashed(dentry))
			goto next;

		if (!d_same_name(dentry, parent, name))
			goto next;

		dentry->d_lockref.count++;
		found = dentry;
		spin_unlock(&dentry->d_lock);
		break;
next:
		spin_unlock(&dentry->d_lock);
	}
	rcu_read_unlock();

	return found;
}

struct dentry *d_alloc_parallel(struct dentry *parent, const struct qstr *name,
				wait_queue_head_t *wq)
{
	struct dentry *dentry;

	/* Check if already cached */
	dentry = d_lookup(parent, name);
	if (dentry)
		return dentry;

	/* Allocate new dentry and mark for lookup */
	dentry = d_alloc(parent, name);
	if (unlikely(!dentry))
		return ERR_PTR(-ENOMEM);
	dentry->d_flags |= DCACHE_PAR_LOOKUP;
	dentry->d_wait = wq;
	return dentry;
}

void __d_lookup_done(struct dentry *dentry)
{
	dentry->d_flags &= ~DCACHE_PAR_LOOKUP;
	wake_up_all(dentry->d_wait);
	dentry->d_wait = NULL;
	INIT_HLIST_NODE(&dentry->d_u.d_alias);
	INIT_LIST_HEAD(&dentry->d_lru);
}

void d_add(struct dentry *entry, struct inode *inode)
{
	struct inode *dir = NULL;
	unsigned n;

	if (inode) {
		spin_lock(&inode->i_lock);
	}
	spin_lock(&entry->d_lock);
	if (unlikely(d_in_lookup(entry))) {
		dir = entry->d_parent->d_inode;
		for (;;) {
			n = dir->i_dir_seq;
			if (!(n & 1) && cmpxchg(&dir->i_dir_seq, n, n + 1) == n)
				break;
			cpu_relax();
		}
		__d_lookup_done(entry);
	}
	if (inode) {
		unsigned add_flags = d_flags_for_inode(inode);
		hlist_add_head(&entry->d_u.d_alias, &inode->i_dentry);
		raw_write_seqcount_begin(&entry->d_seq);
		__d_set_inode_and_type(entry, inode, add_flags);
		raw_write_seqcount_end(&entry->d_seq);
	}
	{
		struct hlist_bl_head *b = d_hash(entry->d_name.hash);
		hlist_bl_lock(b);
		hlist_bl_add_head_rcu(&entry->d_hash, b);
		hlist_bl_unlock(b);
	}
	if (dir)
		smp_store_release(&dir->i_dir_seq, n + 2);
	spin_unlock(&entry->d_lock);
	if (inode)
		spin_unlock(&inode->i_lock);
}

static __initdata unsigned long dhash_entries;

struct kmem_cache *names_cachep __read_mostly;

void __init vfs_caches_init_early(void)
{
	dentry_hashtable = alloc_large_system_hash(
		"Dentry cache", sizeof(struct hlist_bl_head), dhash_entries, 13,
		HASH_EARLY | HASH_ZERO, &d_hash_shift, NULL, 0, 0);
	d_hash_shift = 32 - d_hash_shift;

	inode_init_early();
}

void __init vfs_caches_init(void)
{
	names_cachep = kmem_cache_create_usercopy(
		"names_cache", PATH_MAX, 0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0,
		PATH_MAX, NULL);

	dentry_cache =
		KMEM_CACHE_USERCOPY(dentry,
				    SLAB_RECLAIM_ACCOUNT | SLAB_PANIC |
					    SLAB_MEM_SPREAD | SLAB_ACCOUNT,
				    d_iname);

	inode_init();
	files_init();
	mnt_init();
	chrdev_init();
}
