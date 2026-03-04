#include <linux/fs.h>
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

static inline int dentry_cmp(const struct dentry *dentry,
			     const unsigned char *ct, unsigned tcount)
{
	return memcmp(READ_ONCE(dentry->d_name.name), ct, tcount);
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

/*
 * Simplified dput: just decrement refcount, never kill dentries.
 * For a boot-to-hello-world kernel, dentries are never freed.
 */
void dput(struct dentry *dentry)
{
	if (dentry) {
		if (lockref_put_return(&dentry->d_lockref) >= 0)
			return;
		spin_lock(&dentry->d_lock);
		dentry->d_lockref.count--;
		spin_unlock(&dentry->d_lock);
	}
}

void shrink_dcache_for_umount(struct super_block *sb)
{
	sb->s_root = NULL;
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
	if (!inode)
		return DCACHE_MISS_TYPE;

	if (S_ISDIR(inode->i_mode))
		return DCACHE_DIRECTORY_TYPE;

	if (unlikely(!S_ISREG(inode->i_mode)))
		return DCACHE_SPECIAL_TYPE;

	return DCACHE_REGULAR_TYPE;
}

struct dentry *d_make_root(struct inode *root_inode)
{
	struct dentry *res = NULL;

	if (root_inode) {
		res = __d_alloc(root_inode->i_sb,
				NULL); /* inlined d_alloc_anon */
		if (res) {
			unsigned add_flags = d_flags_for_inode(root_inode);

			spin_lock(&root_inode->i_lock);
			spin_lock(&res->d_lock);
			hlist_add_head(&res->d_u.d_alias,
				       &root_inode->i_dentry);
			raw_write_seqcount_begin(&res->d_seq);
			__d_set_inode_and_type(res, root_inode, add_flags);
			raw_write_seqcount_end(&res->d_seq);
			spin_unlock(&res->d_lock);
			spin_unlock(&root_inode->i_lock);
		} else
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

struct dentry *d_lookup(const struct dentry *parent, const struct qstr *name)
{
	return __d_lookup(parent, name);
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
	unsigned long numentries =
		roundup_pow_of_two(dhash_entries ? dhash_entries : 256);
	unsigned long log2qty = ilog2(numentries);
	dentry_hashtable = memblock_alloc(
		sizeof(struct hlist_bl_head) << log2qty, SMP_CACHE_BYTES);
	if (!dentry_hashtable)
		panic("Failed to allocate dentry hash\n");
	d_hash_shift = 32 - log2qty;
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
}
