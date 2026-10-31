
#include <linux/memblock.h>
#include "internal.h"

static struct kmem_cache *dentry_cache __read_mostly;

static const struct qstr slash_name = QSTR_INIT("/", 1);

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

	kmem_cache_free(dentry_cache, dentry);
}

static void __d_free_external(struct rcu_head *head)
{
	struct dentry *dentry = container_of(head, struct dentry, d_u.d_rcu);
	kfree(external_name(dentry));
	kmem_cache_free(dentry_cache, dentry);
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

static void dentry_unlink_inode(struct dentry *dentry)
	__releases(dentry->d_lock) __releases(dentry->d_inode->i_lock)
{
	struct inode *inode = dentry->d_inode;

	__d_clear_type_and_inode(dentry);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&inode->i_lock);
	/* no ops object sets ->d_iput */
	iput(inode);
}

static void ___d_drop(struct dentry *dentry)
{
	struct hlist_bl_head *b;

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

	/* DCACHE_OP_PRUNE is never set (no ops object sets ->d_prune) */

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
	cond_resched();
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

static struct dentry *__d_alloc(struct super_block *sb, const struct qstr *name)
{
	struct dentry *dentry;
	char *dname;

	dentry = kmem_cache_alloc(dentry_cache, GFP_KERNEL);
	if (!dentry)
		return NULL;

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
			kmem_cache_free(dentry_cache, dentry);
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

	smp_store_release(&dentry->d_name.name, dname);

	dentry->d_lockref.count = 1;
	dentry->d_flags = 0;
	spin_lock_init(&dentry->d_lock);
	dentry->d_inode = NULL;
	dentry->d_parent = dentry;
	dentry->d_sb = sb;
	INIT_HLIST_BL_NODE(&dentry->d_hash);

	return dentry;
}

struct dentry *d_alloc(struct dentry *parent, const struct qstr *name)
{
	struct dentry *dentry = __d_alloc(parent->d_sb, name);
	if (!dentry)
		return NULL;
	spin_lock(&parent->d_lock);

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
		else
			iput(root_inode);
	}
	return res;
}

static inline bool d_same_name(const struct dentry *dentry,
			       const struct qstr *name)
{
	/*
	 * DCACHE_OP_COMPARE is never set on this build (no ->d_compare
	 * ops object exists), so the per-dentry custom compare is
	 * unreachable -- always do a plain name compare.
	 */
	if (dentry->d_name.len != name->len)
		return false;
	return dentry_cmp(dentry, name->name, name->len) == 0;
}

struct dentry *d_lookup(const struct dentry *parent, const struct qstr *name)
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

static void __init dcache_init(void)
{
	dentry_cache = KMEM_CACHE_USERCOPY(
		dentry, SLAB_RECLAIM_ACCOUNT | SLAB_PANIC | SLAB_ACCOUNT,
		d_iname);
}

struct kmem_cache *names_cachep __read_mostly;

void __init vfs_caches_init_early(void)
{
	dcache_init_early();
}

void __init vfs_caches_init(void)
{
	names_cachep = kmem_cache_create_usercopy(
		"names_cache", PATH_MAX, 0, SLAB_HWCACHE_ALIGN | SLAB_PANIC, 0,
		PATH_MAX, NULL);

	dcache_init();
	inode_init();
	files_init();
	mnt_init();
}
