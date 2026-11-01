// SPDX-License-Identifier: GPL-2.0-only
/*
 * fs/dcache.c
 *
 * Complete reimplementation
 * (C) 1997 Thomas Schoebel-Theuer,
 * with heavy changes by Linus Torvalds
 */

/*
 * Notes on the allocation strategy:
 *
 * The dcache is a master of the icache - whenever a dcache entry
 * exists, the inode will always exist. "iput()" is done either when
 * the dcache entry is deleted or garbage collected.
 */

#include <linux/memblock.h>
#include "internal.h"

static const struct qstr slash_name = QSTR_INIT("/", 1);

/*
 * This is the single most critical data structure when it comes
 * to the dcache: the hashtable for lookups. Somebody should try
 * to make this good - I've just made it work.
 *
 * This hash-function tries to avoid losing too many bits of hash
 * information, yet avoid using a prime hash-size or similar.
 */

static unsigned int d_hash_shift __read_mostly;

static struct hlist_bl_head *dentry_hashtable __read_mostly;

static inline struct hlist_bl_head *d_hash(unsigned int hash)
{
	return dentry_hashtable + (hash >> d_hash_shift);
}

#include <asm/word-at-a-time.h>

static inline int dentry_string_cmp(const unsigned char *cs,
				    const unsigned char *ct, unsigned tcount)
{
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

static inline int dentry_cmp(const struct dentry *dentry,
			     const unsigned char *ct, unsigned tcount)
{
	/*
	 * Be careful about RCU walk racing with rename:
	 * use 'READ_ONCE' to fetch the name pointer.
	 *
	 * NOTE! Even if a rename will mean that the length
	 * was not loaded atomically, we don't care. The
	 * RCU walk will check the sequence count eventually,
	 * and catch it. And we won't overrun the buffer,
	 * because we're reading the name pointer atomically,
	 * and a dentry name is guaranteed to be properly
	 * terminated with a NUL byte.
	 *
	 * End result: even if 'len' is wrong, we'll exit
	 * early because the data cannot match (there can
	 * be no NUL in the ct/tcount data)
	 */
	const unsigned char *cs = READ_ONCE(dentry->d_name.name);

	return dentry_string_cmp(cs, ct, tcount);
}

struct external_name {
	union {
		atomic_t count;
	} u;
	unsigned char name[];
};

static inline struct external_name *external_name(struct dentry *dentry)
{
	return container_of(dentry->d_name.name, struct external_name, name[0]);
}

static void __d_free(struct rcu_head *head)
{
	struct dentry *dentry = container_of(head, struct dentry, d_u.d_rcu);

	kfree(dentry);
}

static void __d_free_external(struct rcu_head *head)
{
	struct dentry *dentry = container_of(head, struct dentry, d_u.d_rcu);
	kfree(external_name(dentry));
	kfree(dentry);
}

static inline int dname_external(const struct dentry *dentry)
{
	return dentry->d_name.name != dentry->d_iname;
}

static inline void __d_set_inode_and_type(struct dentry *dentry,
					  struct inode *inode,
					  unsigned type_flags)
{
	unsigned flags;

	dentry->d_inode = inode;
	flags = READ_ONCE(dentry->d_flags);
	flags &= ~DCACHE_ENTRY_TYPE;
	flags |= type_flags;
	smp_store_release(&dentry->d_flags, flags);
}

static inline void __d_clear_type_and_inode(struct dentry *dentry)
{
	unsigned flags = READ_ONCE(dentry->d_flags);

	flags &= ~DCACHE_ENTRY_TYPE;
	WRITE_ONCE(dentry->d_flags, flags);
	dentry->d_inode = NULL;
}

static void dentry_free(struct dentry *dentry)
{
	if (unlikely(dname_external(dentry))) {
		struct external_name *p = external_name(dentry);
		if (likely(atomic_dec_and_test(&p->u.count))) {
			__d_free_external(&dentry->d_u.d_rcu);
			return;
		}
	}
	/*
	 * Upstream's DCACHE_NORCU arm: free at once. Every dentry qualifies
	 * here: one CPU, no preemption, and d_lookup() (the only lockless
	 * reader) never runs while a dentry is being killed, so no reader
	 * can hold a dentry that dput() has unhashed and is freeing.
	 */
	__d_free(&dentry->d_u.d_rcu);
}

/*
 * Release the dentry's inode, using the filesystem
 * d_iput() operation if defined.
 */
static void dentry_unlink_inode(struct dentry *dentry)
	__releases(dentry->d_lock) __releases(dentry->d_inode->i_lock)
{
	struct inode *inode = dentry->d_inode;

	__d_clear_type_and_inode(dentry);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&inode->i_lock);
	/*
	 * No ops object sets ->d_iput, and inodes are never released on this
	 * kernel (no i_count, no eviction), so there is no iput().
	 */
}

static void ___d_drop(struct dentry *dentry)
{
	struct hlist_bl_head *b;

	/*
	 * Hashed dentries are normally on the dentry hashtable,
	 * with the exception of those newly allocated by
	 * d_obtain_root, which are always IS_ROOT:
	 */
	if (unlikely(IS_ROOT(dentry)))
		b = &dentry->d_sb->s_roots;
	else
		b = d_hash(dentry->d_name.hash);

	hlist_bl_lock(b);
	__hlist_bl_del(&dentry->d_hash);
	hlist_bl_unlock(b);
}

static void __dentry_kill(struct dentry *dentry)
{
	struct dentry *parent = NULL;
	if (!IS_ROOT(dentry))
		parent = dentry->d_parent;

	lockref_mark_dead(&dentry->d_lockref);

	if (!d_unhashed(dentry)) {
		___d_drop(dentry);
		dentry->d_hash.pprev = NULL;
	}
	/* d_child sibling list is never iterated; no unlink needed */
	if (parent)
		spin_unlock(&parent->d_lock);
	if (dentry->d_inode)
		dentry_unlink_inode(dentry);
	else
		spin_unlock(&dentry->d_lock);
	/* no ops object sets ->d_release */

	dentry_free(dentry);
}

/*
 * Single task, uniprocessor: nothing else can hold these locks, so the
 * trylock/lock_parent retry dance of upstream is not needed.
 */
static struct dentry *dentry_kill(struct dentry *dentry)
	__releases(dentry->d_lock)
{
	struct dentry *parent = NULL;

	if (dentry->d_inode)
		spin_lock(&dentry->d_inode->i_lock);
	if (!IS_ROOT(dentry)) {
		parent = dentry->d_parent;
		spin_lock(&parent->d_lock);
	}
	__dentry_kill(dentry);
	return parent;
}

/*
 * No dentry is ever cached once its last reference is dropped (the only
 * dentry_operations ever set was always_delete_dentry), so dput() either
 * decrements or kills.
 */
void dput(struct dentry *dentry)
{
	while (dentry) {
		spin_lock(&dentry->d_lock);
		if (dentry->d_lockref.count > 1) {
			dentry->d_lockref.count--;
			spin_unlock(&dentry->d_lock);
			return;
		}
		dentry = dentry_kill(dentry);
	}
}

/* This must be called with d_lock held */
static inline void __dget_dlock(struct dentry *dentry)
{
	dentry->d_lockref.count++;
}

struct dentry *dget_parent(struct dentry *dentry)
{
	struct dentry *ret;

	ret = dentry->d_parent;
	spin_lock(&ret->d_lock);
	BUG_ON(!ret->d_lockref.count);
	ret->d_lockref.count++;
	spin_unlock(&ret->d_lock);
	return ret;
}

/**
 * __d_alloc	-	allocate a dcache entry
 * @sb: filesystem it will belong to
 * @name: qstr of the name
 *
 * Allocates a dentry. It returns %NULL if there is insufficient memory
 * available. On a success the dentry is returned. The name passed in is
 * copied and the copy passed in may be reused after this call.
 */

static struct dentry *__d_alloc(struct super_block *sb, const struct qstr *name)
{
	struct dentry *dentry;
	char *dname;

	dentry = kmalloc(sizeof(*dentry), GFP_KERNEL);
	if (!dentry)
		return NULL;

	/*
	 * We guarantee that the inline name is always NUL-terminated.
	 * This way the memcpy() done by the name switching in rename
	 * will still always have a NUL at the end, even if we might
	 * be overwriting an internal NUL character
	 */
	dentry->d_iname[DNAME_INLINE_LEN - 1] = 0;
	if (unlikely(!name)) {
		name = &slash_name;
		dname = dentry->d_iname;
	} else if (name->len > DNAME_INLINE_LEN - 1) {
		size_t size = offsetof(struct external_name, name[1]);
		struct external_name *p =
			kmalloc(size + name->len,
				GFP_KERNEL_ACCOUNT | __GFP_RECLAIMABLE);
		if (!p) {
			kfree(dentry);
			return NULL;
		}
		atomic_set(&p->u.count, 1);
		dname = p->name;
	} else {
		dname = dentry->d_iname;
	}

	dentry->d_name.len = name->len;
	dentry->d_name.hash = name->hash;
	memcpy(dname, name->name, name->len);
	dname[name->len] = 0;

	/* Make sure we always see the terminating NUL character */
	smp_store_release(&dentry->d_name.name, dname); /* ^^^ */

	dentry->d_lockref.count = 1;
	dentry->d_flags = 0;
	spin_lock_init(&dentry->d_lock);
	dentry->d_inode = NULL;
	dentry->d_parent = dentry;
	dentry->d_sb = sb;
	INIT_HLIST_BL_NODE(&dentry->d_hash);

	return dentry;
}

/**
 * d_alloc	-	allocate a dcache entry
 * @parent: parent of entry to allocate
 * @name: qstr of the name
 *
 * Allocates a dentry. It returns %NULL if there is insufficient memory
 * available. On a success the dentry is returned. The name passed in is
 * copied and the copy passed in may be reused after this call.
 */
struct dentry *d_alloc(struct dentry *parent, const struct qstr *name)
{
	struct dentry *dentry = __d_alloc(parent->d_sb, name);
	if (!dentry)
		return NULL;
	spin_lock(&parent->d_lock);

	/*
	 * don't need child lock because it is not subject
	 * to concurrency here
	 */
	__dget_dlock(parent);
	dentry->d_parent = parent;
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
		goto type_determined;
	}

	if (unlikely(!S_ISREG(inode->i_mode)))
		add_flags = DCACHE_SPECIAL_TYPE;

type_determined:
	return add_flags;
}

static void __d_instantiate(struct dentry *dentry, struct inode *inode)
{
	unsigned add_flags = d_flags_for_inode(inode);

	spin_lock(&dentry->d_lock);
	__d_set_inode_and_type(dentry, inode, add_flags);
	spin_unlock(&dentry->d_lock);
}

/**
 * d_instantiate - fill in inode information for a dentry
 * @entry: dentry to complete
 * @inode: inode to attach to this dentry
 *
 * Fill in inode information in the entry.
 *
 * This turns negative dentries into productive full members
 * of society.
 *
 * NOTE! This assumes that the inode count has been incremented
 * (or otherwise set) by the caller to indicate that it is now
 * in use by the dcache.
 */

void d_instantiate(struct dentry *entry, struct inode *inode)
{
	if (inode) {
		spin_lock(&inode->i_lock);
		__d_instantiate(entry, inode);
		spin_unlock(&inode->i_lock);
	}
}

struct dentry *d_make_root(struct inode *root_inode)
{
	struct dentry *res = NULL;

	if (root_inode) {
		res = __d_alloc(root_inode->i_sb, NULL);
		if (res)
			d_instantiate(res, root_inode);
	}
	return res;
}

static inline bool d_same_name(const struct dentry *dentry,
			       const struct qstr *name)
{
	if (dentry->d_name.len != name->len)
		return false;
	return dentry_cmp(dentry, name->name, name->len) == 0;
}

/**
 * d_lookup - search for a dentry
 * @parent: parent dentry
 * @name: qstr of name we wish to find
 * Returns: dentry, or NULL
 *
 * d_lookup searches the children of the parent dentry for the name in
 * question. If the dentry is found its reference count is incremented and the
 * dentry is returned. The caller must use dput to free the entry when it has
 * finished using it. %NULL is returned if the dentry does not exist.
 */
struct dentry *d_lookup(const struct dentry *parent, const struct qstr *name)
{
	unsigned int hash = name->hash;
	struct hlist_bl_head *b = d_hash(hash);
	struct hlist_bl_node *node;
	struct dentry *found = NULL;
	struct dentry *dentry;

	/*
	 * Note: There is significant duplication with __d_lookup_rcu which is
	 * required to prevent single threaded performance regressions
	 * especially on architectures where smp_rmb (in seqcounts) are costly.
	 * Keep the two functions in sync.
	 */

	/*
	 * The hash list is protected using RCU.
	 *
	 * Take d_lock when comparing a candidate dentry, to avoid races
	 * with d_move().
	 *
	 * It is possible that concurrent renames can mess up our list
	 * walk here and result in missing our dentry, resulting in the
	 * false-negative result. d_lookup() protects against concurrent
	 * renames using rename_lock seqlock.
	 *
	 * See Documentation/filesystems/path-lookup.txt for more details.
	 */
	rcu_read_lock();

	hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash) {
		if (dentry->d_name.hash != hash)
			continue;

		spin_lock(&dentry->d_lock);
		if (dentry->d_parent != parent)
			goto next;
		if (d_unhashed(dentry))
			goto next;

		if (!d_same_name(dentry, name))
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

static void __d_rehash(struct dentry *entry)
{
	struct hlist_bl_head *b = d_hash(entry->d_name.hash);

	hlist_bl_lock(b);
	hlist_bl_add_head_rcu(&entry->d_hash, b);
	hlist_bl_unlock(b);
}

/* inode->i_lock held if inode is non-NULL */

static inline void __d_add(struct dentry *dentry, struct inode *inode)
{
	spin_lock(&dentry->d_lock);
	if (inode) {
		unsigned add_flags = d_flags_for_inode(inode);
		__d_set_inode_and_type(dentry, inode, add_flags);
	}
	__d_rehash(dentry);
	spin_unlock(&dentry->d_lock);
	if (inode)
		spin_unlock(&inode->i_lock);
}

/**
 * d_add - add dentry to hash queues
 * @entry: dentry to add
 * @inode: The inode to attach to this dentry
 *
 * This adds the entry to the hash queues and initializes @inode.
 * The entry was actually filled in earlier during d_alloc().
 */

void d_add(struct dentry *entry, struct inode *inode)
{
	if (inode) {
		spin_lock(&inode->i_lock);
	}
	__d_add(entry, inode);
}

static void __init dcache_init_early(void)
{
	dentry_hashtable = alloc_large_system_hash(
		"Dentry cache", sizeof(struct hlist_bl_head), 0, 13,
		HASH_EARLY | HASH_ZERO, &d_hash_shift, NULL, 0, 0);
	d_hash_shift = 32 - d_hash_shift;
}

void __init vfs_caches_init_early(void)
{
	dcache_init_early();
}

void __init vfs_caches_init(void)
{
	mnt_init();
}
