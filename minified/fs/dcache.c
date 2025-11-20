 

#include <linux/ratelimit.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/fscrypt.h>
#include <linux/fsnotify.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/hash.h>
#include <linux/cache.h>
#include <linux/export.h>
#include <linux/security.h>
#include <linux/seqlock.h>
#include <linux/memblock.h>
#include <linux/bit_spinlock.h>
#include <linux/rculist_bl.h>
#include <linux/list_lru.h>
#include "internal.h"
#include "mount.h"

int sysctl_vfs_cache_pressure __read_mostly = 100;

__cacheline_aligned_in_smp DEFINE_SEQLOCK(rename_lock);


static struct kmem_cache *dentry_cache __read_mostly;

const struct qstr empty_name = QSTR_INIT("", 0);
const struct qstr slash_name = QSTR_INIT("/", 1);
const struct qstr dotdot_name = QSTR_INIT("..", 2);

static unsigned int d_hash_shift __read_mostly;

static struct hlist_bl_head *dentry_hashtable __read_mostly;

static inline struct hlist_bl_head *d_hash(unsigned int hash)
{
	return dentry_hashtable + (hash >> d_hash_shift);
}

#define IN_LOOKUP_SHIFT 10
static struct hlist_bl_head in_lookup_hashtable[1 << IN_LOOKUP_SHIFT];

static inline struct hlist_bl_head *in_lookup_hash(const struct dentry *parent,
					unsigned int hash)
{
	hash += (unsigned long) parent / L1_CACHE_BYTES;
	return in_lookup_hashtable + hash_32(hash, IN_LOOKUP_SHIFT);
}

struct dentry_stat_t {
	long nr_dentry;
	long nr_unused;
	long age_limit;		
	long want_pages;	
	long nr_negative;	
	long dummy;		
};

static DEFINE_PER_CPU(long, nr_dentry);
static DEFINE_PER_CPU(long, nr_dentry_unused);
static DEFINE_PER_CPU(long, nr_dentry_negative);

#include <asm/word-at-a-time.h>

static inline int dentry_string_cmp(const unsigned char *cs, const unsigned char *ct, unsigned tcount)
{
	unsigned long a,b,mask;

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

static inline int dentry_cmp(const struct dentry *dentry, const unsigned char *ct, unsigned tcount)
{
	
	const unsigned char *cs = READ_ONCE(dentry->d_name.name);

	return dentry_string_cmp(cs, ct, tcount);
}

struct external_name {
	union {
		atomic_t count;
		struct rcu_head head;
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

void take_dentry_name_snapshot(struct name_snapshot *name, struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	name->name = dentry->d_name;
	if (unlikely(dname_external(dentry))) {
		atomic_inc(&external_name(dentry)->u.count);
	} else {
		memcpy(name->inline_name, dentry->d_iname,
		       dentry->d_name.len + 1);
		name->name.name = name->inline_name;
	}
	spin_unlock(&dentry->d_lock);
}

void release_dentry_name_snapshot(struct name_snapshot *name)
{
	if (unlikely(name->name.name != name->inline_name)) {
		struct external_name *p;
		p = container_of(name->name.name, struct external_name, name[0]);
		if (unlikely(atomic_dec_and_test(&p->u.count)))
			kfree_rcu(p, u.head);
	}
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

static inline void __d_clear_type_and_inode(struct dentry *dentry)
{
	unsigned flags = READ_ONCE(dentry->d_flags);

	flags &= ~(DCACHE_ENTRY_TYPE | DCACHE_FALLTHRU);
	WRITE_ONCE(dentry->d_flags, flags);
	dentry->d_inode = NULL;
	if (dentry->d_flags & DCACHE_LRU_LIST)
		this_cpu_inc(nr_dentry_negative);
}

static void dentry_free(struct dentry *dentry)
{
	WARN_ON(!hlist_unhashed(&dentry->d_u.d_alias));
	if (unlikely(dname_external(dentry))) {
		struct external_name *p = external_name(dentry);
		if (likely(atomic_dec_and_test(&p->u.count))) {
			call_rcu(&dentry->d_u.d_rcu, __d_free_external);
			return;
		}
	}
	
	if (dentry->d_flags & DCACHE_NORCU)
		__d_free(&dentry->d_u.d_rcu);
	else
		call_rcu(&dentry->d_u.d_rcu, __d_free);
}

static void dentry_unlink_inode(struct dentry * dentry)
	__releases(dentry->d_lock)
	__releases(dentry->d_inode->i_lock)
{
	struct inode *inode = dentry->d_inode;

	raw_write_seqcount_begin(&dentry->d_seq);
	__d_clear_type_and_inode(dentry);
	hlist_del_init(&dentry->d_u.d_alias);
	raw_write_seqcount_end(&dentry->d_seq);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&inode->i_lock);
	if (!inode->i_nlink)
		fsnotify_inoderemove(inode);
	if (dentry->d_op && dentry->d_op->d_iput)
		dentry->d_op->d_iput(dentry, inode);
	else
		iput(inode);
}

#define D_FLAG_VERIFY(dentry,x) WARN_ON_ONCE(((dentry)->d_flags & (DCACHE_LRU_LIST | DCACHE_SHRINK_LIST)) != (x))
static void d_lru_add(struct dentry *dentry)
{
	D_FLAG_VERIFY(dentry, 0);
	dentry->d_flags |= DCACHE_LRU_LIST;
	this_cpu_inc(nr_dentry_unused);
	if (d_is_negative(dentry))
		this_cpu_inc(nr_dentry_negative);
	WARN_ON_ONCE(!list_lru_add(&dentry->d_sb->s_dentry_lru, &dentry->d_lru));
}

static void d_lru_del(struct dentry *dentry)
{
	D_FLAG_VERIFY(dentry, DCACHE_LRU_LIST);
	dentry->d_flags &= ~DCACHE_LRU_LIST;
	this_cpu_dec(nr_dentry_unused);
	if (d_is_negative(dentry))
		this_cpu_dec(nr_dentry_negative);
	WARN_ON_ONCE(!list_lru_del(&dentry->d_sb->s_dentry_lru, &dentry->d_lru));
}

static void d_shrink_del(struct dentry *dentry)
{
	D_FLAG_VERIFY(dentry, DCACHE_SHRINK_LIST | DCACHE_LRU_LIST);
	list_del_init(&dentry->d_lru);
	dentry->d_flags &= ~(DCACHE_SHRINK_LIST | DCACHE_LRU_LIST);
	this_cpu_dec(nr_dentry_unused);
}

static void d_shrink_add(struct dentry *dentry, struct list_head *list)
{
	D_FLAG_VERIFY(dentry, 0);
	list_add(&dentry->d_lru, list);
	dentry->d_flags |= DCACHE_SHRINK_LIST | DCACHE_LRU_LIST;
	this_cpu_inc(nr_dentry_unused);
}

static void d_lru_isolate(struct list_lru_one *lru, struct dentry *dentry)
{
	D_FLAG_VERIFY(dentry, DCACHE_LRU_LIST);
	dentry->d_flags &= ~DCACHE_LRU_LIST;
	this_cpu_dec(nr_dentry_unused);
	if (d_is_negative(dentry))
		this_cpu_dec(nr_dentry_negative);
	list_lru_isolate(lru, &dentry->d_lru);
}

static void d_lru_shrink_move(struct list_lru_one *lru, struct dentry *dentry,
			      struct list_head *list)
{
	D_FLAG_VERIFY(dentry, DCACHE_LRU_LIST);
	dentry->d_flags |= DCACHE_SHRINK_LIST;
	if (d_is_negative(dentry))
		this_cpu_dec(nr_dentry_negative);
	list_lru_isolate_move(lru, &dentry->d_lru, list);
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

void __d_drop(struct dentry *dentry)
{
	if (!d_unhashed(dentry)) {
		___d_drop(dentry);
		dentry->d_hash.pprev = NULL;
		write_seqcount_invalidate(&dentry->d_seq);
	}
}

void d_drop(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
}

static inline void dentry_unlist(struct dentry *dentry, struct dentry *parent)
{
	struct dentry *next;
	
	dentry->d_flags |= DCACHE_DENTRY_KILLED;
	if (unlikely(list_empty(&dentry->d_child)))
		return;
	__list_del_entry(&dentry->d_child);
	
	while (dentry->d_child.next != &parent->d_subdirs) {
		next = list_entry(dentry->d_child.next, struct dentry, d_child);
		if (likely(!(next->d_flags & DCACHE_DENTRY_CURSOR)))
			break;
		dentry->d_child.next = next->d_child.next;
	}
}

static void __dentry_kill(struct dentry *dentry)
{
	struct dentry *parent = NULL;
	bool can_free = true;
	if (!IS_ROOT(dentry))
		parent = dentry->d_parent;

	
	lockref_mark_dead(&dentry->d_lockref);

	
	if (dentry->d_flags & DCACHE_OP_PRUNE)
		dentry->d_op->d_prune(dentry);

	if (dentry->d_flags & DCACHE_LRU_LIST) {
		if (!(dentry->d_flags & DCACHE_SHRINK_LIST))
			d_lru_del(dentry);
	}
	
	__d_drop(dentry);
	dentry_unlist(dentry, parent);
	if (parent)
		spin_unlock(&parent->d_lock);
	if (dentry->d_inode)
		dentry_unlink_inode(dentry);
	else
		spin_unlock(&dentry->d_lock);
	this_cpu_dec(nr_dentry);
	if (dentry->d_op && dentry->d_op->d_release)
		dentry->d_op->d_release(dentry);

	spin_lock(&dentry->d_lock);
	if (dentry->d_flags & DCACHE_SHRINK_LIST) {
		dentry->d_flags |= DCACHE_MAY_FREE;
		can_free = false;
	}
	spin_unlock(&dentry->d_lock);
	if (likely(can_free))
		dentry_free(dentry);
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

static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *parent = dentry->d_parent;
	if (IS_ROOT(dentry))
		return NULL;
	if (likely(spin_trylock(&parent->d_lock)))
		return parent;
	return __lock_parent(dentry);
}

static inline bool retain_dentry(struct dentry *dentry)
{
	WARN_ON(d_in_lookup(dentry));

	
	if (unlikely(d_unhashed(dentry)))
		return false;

	if (unlikely(dentry->d_flags & DCACHE_DISCONNECTED))
		return false;

	if (unlikely(dentry->d_flags & DCACHE_OP_DELETE)) {
		if (dentry->d_op->d_delete(dentry))
			return false;
	}

	if (unlikely(dentry->d_flags & DCACHE_DONTCACHE))
		return false;

	
	dentry->d_lockref.count--;
	if (unlikely(!(dentry->d_flags & DCACHE_LRU_LIST)))
		d_lru_add(dentry);
	else if (unlikely(!(dentry->d_flags & DCACHE_REFERENCED)))
		dentry->d_flags |= DCACHE_REFERENCED;
	return true;
}

void d_mark_dontcache(struct inode *inode)
{
	struct dentry *de;

	spin_lock(&inode->i_lock);
	hlist_for_each_entry(de, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&de->d_lock);
		de->d_flags |= DCACHE_DONTCACHE;
		spin_unlock(&de->d_lock);
	}
	inode->i_state |= I_DONTCACHE;
	spin_unlock(&inode->i_lock);
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
	parent = lock_parent(dentry);
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
	int ret;
	unsigned int d_flags;

	
	if (unlikely(dentry->d_flags & DCACHE_OP_DELETE))
		return lockref_put_or_lock(&dentry->d_lockref);

	
	ret = lockref_put_return(&dentry->d_lockref);

	
	if (unlikely(ret < 0)) {
		spin_lock(&dentry->d_lock);
		if (dentry->d_lockref.count > 1) {
			dentry->d_lockref.count--;
			spin_unlock(&dentry->d_lock);
			return true;
		}
		return false;
	}

	
	if (ret)
		return true;

	
	smp_rmb();
	d_flags = READ_ONCE(dentry->d_flags);
	d_flags &= DCACHE_REFERENCED | DCACHE_LRU_LIST |
			DCACHE_DISCONNECTED | DCACHE_DONTCACHE;

	
	if (d_flags == (DCACHE_REFERENCED | DCACHE_LRU_LIST) && !d_unhashed(dentry))
		return true;

	
	spin_lock(&dentry->d_lock);

	
	if (dentry->d_lockref.count) {
		spin_unlock(&dentry->d_lock);
		return true;
	}

	
	dentry->d_lockref.count = 1;
	return false;
}

void dput(struct dentry *dentry)
{
	while (dentry) {
		might_sleep();

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

static void __dput_to_list(struct dentry *dentry, struct list_head *list)
__must_hold(&dentry->d_lock)
{
	if (dentry->d_flags & DCACHE_SHRINK_LIST) {
		
		--dentry->d_lockref.count;
	} else {
		if (dentry->d_flags & DCACHE_LRU_LIST)
			d_lru_del(dentry);
		if (!--dentry->d_lockref.count)
			d_shrink_add(dentry, list);
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
	if (!retain_dentry(dentry))
		__dput_to_list(dentry, list);
	spin_unlock(&dentry->d_lock);
}

static inline void __dget_dlock(struct dentry *dentry)
{
	dentry->d_lockref.count++;
}

static inline void __dget(struct dentry *dentry)
{
	lockref_get(&dentry->d_lockref);
}

struct dentry *dget_parent(struct dentry *dentry)
{
	int gotref;
	struct dentry *ret;
	unsigned seq;

	
	rcu_read_lock();
	seq = raw_seqcount_begin(&dentry->d_seq);
	ret = READ_ONCE(dentry->d_parent);
	gotref = lockref_get_not_zero(&ret->d_lockref);
	rcu_read_unlock();
	if (likely(gotref)) {
		if (!read_seqcount_retry(&dentry->d_seq, seq))
			return ret;
		dput(ret);
	}

repeat:
	
	rcu_read_lock();
	ret = dentry->d_parent;
	spin_lock(&ret->d_lock);
	if (unlikely(ret != dentry->d_parent)) {
		spin_unlock(&ret->d_lock);
		rcu_read_unlock();
		goto repeat;
	}
	rcu_read_unlock();
	BUG_ON(!ret->d_lockref.count);
	ret->d_lockref.count++;
	spin_unlock(&ret->d_lock);
	return ret;
}

static struct dentry * __d_find_any_alias(struct inode *inode)
{
	struct dentry *alias;

	if (hlist_empty(&inode->i_dentry))
		return NULL;
	alias = hlist_entry(inode->i_dentry.first, struct dentry, d_u.d_alias);
	__dget(alias);
	return alias;
}

struct dentry *d_find_any_alias(struct inode *inode)
{
	struct dentry *de;

	spin_lock(&inode->i_lock);
	de = __d_find_any_alias(inode);
	spin_unlock(&inode->i_lock);
	return de;
}

static struct dentry *__d_find_alias(struct inode *inode)
{
	struct dentry *alias;

	if (S_ISDIR(inode->i_mode))
		return __d_find_any_alias(inode);

	hlist_for_each_entry(alias, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&alias->d_lock);
 		if (!d_unhashed(alias)) {
			__dget_dlock(alias);
			spin_unlock(&alias->d_lock);
			return alias;
		}
		spin_unlock(&alias->d_lock);
	}
	return NULL;
}

struct dentry *d_find_alias(struct inode *inode)
{
	struct dentry *de = NULL;

	if (!hlist_empty(&inode->i_dentry)) {
		spin_lock(&inode->i_lock);
		de = __d_find_alias(inode);
		spin_unlock(&inode->i_lock);
	}
	return de;
}

struct dentry *d_find_alias_rcu(struct inode *inode)
{
	struct hlist_head *l = &inode->i_dentry;
	struct dentry *de = NULL;

	spin_lock(&inode->i_lock);
	 
	 
	if (likely(!(inode->i_state & I_FREEING) && !hlist_empty(l))) {
		if (S_ISDIR(inode->i_mode)) {
			de = hlist_entry(l->first, struct dentry, d_u.d_alias);
		} else {
			hlist_for_each_entry(de, l, d_u.d_alias)
				if (!d_unhashed(de))
					break;
		}
	}
	spin_unlock(&inode->i_lock);
	return de;
}

void d_prune_aliases(struct inode *inode)
{
	struct dentry *dentry;
restart:
	spin_lock(&inode->i_lock);
	hlist_for_each_entry(dentry, &inode->i_dentry, d_u.d_alias) {
		spin_lock(&dentry->d_lock);
		if (!dentry->d_lockref.count) {
			struct dentry *parent = lock_parent(dentry);
			if (likely(!dentry->d_lockref.count)) {
				__dentry_kill(dentry);
				dput(parent);
				goto restart;
			}
			if (parent)
				spin_unlock(&parent->d_lock);
		}
		spin_unlock(&dentry->d_lock);
	}
	spin_unlock(&inode->i_lock);
}

static bool shrink_lock_dentry(struct dentry *dentry)
{
	struct inode *inode;
	struct dentry *parent;

	if (dentry->d_lockref.count)
		return false;

	inode = dentry->d_inode;
	if (inode && unlikely(!spin_trylock(&inode->i_lock))) {
		spin_unlock(&dentry->d_lock);
		spin_lock(&inode->i_lock);
		spin_lock(&dentry->d_lock);
		if (unlikely(dentry->d_lockref.count))
			goto out;
		
		if (unlikely(inode != dentry->d_inode))
			goto out;
	}

	parent = dentry->d_parent;
	if (IS_ROOT(dentry) || likely(spin_trylock(&parent->d_lock)))
		return true;

	spin_unlock(&dentry->d_lock);
	spin_lock(&parent->d_lock);
	if (unlikely(parent != dentry->d_parent)) {
		spin_unlock(&parent->d_lock);
		spin_lock(&dentry->d_lock);
		goto out;
	}
	spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	if (likely(!dentry->d_lockref.count))
		return true;
	spin_unlock(&parent->d_lock);
out:
	if (inode)
		spin_unlock(&inode->i_lock);
	return false;
}

void shrink_dentry_list(struct list_head *list)
{
	/* Stub: not needed for minimal boot */
}

static enum lru_status dentry_lru_isolate(struct list_head *item,
		struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	return LRU_SKIP; /* Stub */
}

long prune_dcache_sb(struct super_block *sb, struct shrink_control *sc)
{
	LIST_HEAD(dispose);
	long freed;

	freed = list_lru_shrink_walk(&sb->s_dentry_lru, sc,
				     dentry_lru_isolate, &dispose);
	shrink_dentry_list(&dispose);
	return freed;
}

static enum lru_status dentry_lru_isolate_shrink(struct list_head *item,
		struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	return LRU_SKIP; /* Stub */
}

void shrink_dcache_sb(struct super_block *sb)
{
	do {
		LIST_HEAD(dispose);

		list_lru_walk(&sb->s_dentry_lru,
			dentry_lru_isolate_shrink, &dispose, 1024);
		shrink_dentry_list(&dispose);
	} while (list_lru_count(&sb->s_dentry_lru) > 0);
}

enum d_walk_ret {
	D_WALK_CONTINUE,
	D_WALK_QUIT,
	D_WALK_NORETRY,
	D_WALK_SKIP,
};

static void d_walk(struct dentry *parent, void *data,
		   enum d_walk_ret (*enter)(void *, struct dentry *))
{
	/* Stub: not needed for minimal boot */
}

struct check_mount {
	struct vfsmount *mnt;
	unsigned int mounted;
};

static enum d_walk_ret path_check_mount(void *data, struct dentry *dentry)
{
	/* Stub: simplified mount checking for minimal kernel */
	return D_WALK_CONTINUE;
}

int path_has_submounts(const struct path *parent)
{
	/* Stub: simplified submount detection for minimal kernel */
	return 0;
}

int d_set_mounted(struct dentry *dentry)
{
	/* Stub: simplified mount point marking for minimal kernel */
	spin_lock(&dentry->d_lock);
	if (!d_unlinked(dentry) && !d_mountpoint(dentry)) {
		dentry->d_flags |= DCACHE_MOUNTED;
		spin_unlock(&dentry->d_lock);
		return 0;
	}
	spin_unlock(&dentry->d_lock);
	return -EBUSY;
}

struct select_data {
	struct dentry *start;
	union {
		long found;
		struct dentry *victim;
	};
	struct list_head dispose;
};

static enum d_walk_ret select_collect(void *_data, struct dentry *dentry)
{
	/* Stub: simplified dentry collection for minimal kernel */
	struct select_data *data = _data;

	if (data->start == dentry)
		return D_WALK_CONTINUE;

	if (!(dentry->d_flags & DCACHE_SHRINK_LIST) && !dentry->d_lockref.count) {
		d_shrink_add(dentry, &data->dispose);
		data->found++;
	}
	return D_WALK_CONTINUE;
}

static enum d_walk_ret select_collect2(void *_data, struct dentry *dentry)
{
	/* Stub: simplified dentry collection for minimal kernel */
	struct select_data *data = _data;

	if (data->start == dentry)
		return D_WALK_CONTINUE;

	if (!dentry->d_lockref.count)
		d_shrink_add(dentry, &data->dispose);
	return D_WALK_CONTINUE;
}

void shrink_dcache_parent(struct dentry *parent)
{
	/* Stub: minimal dcache shrinking for simple kernel */
	struct select_data data = {.start = parent};

	INIT_LIST_HEAD(&data.dispose);
	d_walk(parent, &data, select_collect);
	if (!list_empty(&data.dispose))
		shrink_dentry_list(&data.dispose);
}

static enum d_walk_ret umount_check(void *_data, struct dentry *dentry)
{
	
	if (!list_empty(&dentry->d_subdirs))
		return D_WALK_CONTINUE;

	
	if (dentry == _data && dentry->d_lockref.count == 1)
		return D_WALK_CONTINUE;

	printk(KERN_ERR "BUG: Dentry %p{i=%lx,n=%pd} "
			" still in use (%d) [unmount of %s %s]\n",
		       dentry,
		       dentry->d_inode ?
		       dentry->d_inode->i_ino : 0UL,
		       dentry,
		       dentry->d_lockref.count,
		       dentry->d_sb->s_type->name,
		       dentry->d_sb->s_id);
	WARN_ON(1);
	return D_WALK_CONTINUE;
}

static void do_one_tree(struct dentry *dentry)
{
	shrink_dcache_parent(dentry);
	d_walk(dentry, dentry, umount_check);
	d_drop(dentry);
	dput(dentry);
}

void shrink_dcache_for_umount(struct super_block *sb)
{
	struct dentry *dentry;

	WARN(down_read_trylock(&sb->s_umount), "s_umount should've been locked");

	dentry = sb->s_root;
	sb->s_root = NULL;
	do_one_tree(dentry);

	while (!hlist_bl_empty(&sb->s_roots)) {
		dentry = dget(hlist_bl_entry(hlist_bl_first(&sb->s_roots), struct dentry, d_hash));
		do_one_tree(dentry);
	}
}

static enum d_walk_ret find_submount(void *_data, struct dentry *dentry)
{
	struct dentry **victim = _data;
	if (d_mountpoint(dentry)) {
		__dget_dlock(dentry);
		*victim = dentry;
		return D_WALK_QUIT;
	}
	return D_WALK_CONTINUE;
}

void d_invalidate(struct dentry *dentry)
{
	bool had_submounts = false;
	spin_lock(&dentry->d_lock);
	if (d_unhashed(dentry)) {
		spin_unlock(&dentry->d_lock);
		return;
	}
	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);

	
	if (!dentry->d_inode)
		return;

	shrink_dcache_parent(dentry);
	for (;;) {
		struct dentry *victim = NULL;
		d_walk(dentry, &victim, find_submount);
		if (!victim) {
			if (had_submounts)
				shrink_dcache_parent(dentry);
			return;
		}
		had_submounts = true;
		detach_mounts(victim);
		dput(victim);
	}
}

 
static struct dentry *__d_alloc(struct super_block *sb, const struct qstr *name)
{
	struct dentry *dentry;
	char *dname;
	int err;

	dentry = kmem_cache_alloc_lru(dentry_cache, &sb->s_dentry_lru,
				      GFP_KERNEL);
	if (!dentry)
		return NULL;

	
	dentry->d_iname[DNAME_INLINE_LEN-1] = 0;
	if (unlikely(!name)) {
		name = &slash_name;
		dname = dentry->d_iname;
	} else if (name->len > DNAME_INLINE_LEN-1) {
		size_t size = offsetof(struct external_name, name[1]);
		struct external_name *p = kmalloc(size + name->len,
						  GFP_KERNEL_ACCOUNT |
						  __GFP_RECLAIMABLE);
		if (!p) {
			kmem_cache_free(dentry_cache, dentry); 
			return NULL;
		}
		atomic_set(&p->u.count, 1);
		dname = p->name;
	} else  {
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
	seqcount_spinlock_init(&dentry->d_seq, &dentry->d_lock);
	dentry->d_inode = NULL;
	dentry->d_parent = dentry;
	dentry->d_sb = sb;
	dentry->d_op = NULL;
	dentry->d_fsdata = NULL;
	INIT_HLIST_BL_NODE(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_lru);
	INIT_LIST_HEAD(&dentry->d_subdirs);
	INIT_HLIST_NODE(&dentry->d_u.d_alias);
	INIT_LIST_HEAD(&dentry->d_child);
	d_set_d_op(dentry, dentry->d_sb->s_d_op);

	if (dentry->d_op && dentry->d_op->d_init) {
		err = dentry->d_op->d_init(dentry);
		if (err) {
			if (dname_external(dentry))
				kfree(external_name(dentry));
			kmem_cache_free(dentry_cache, dentry);
			return NULL;
		}
	}

	this_cpu_inc(nr_dentry);

	return dentry;
}

struct dentry *d_alloc(struct dentry * parent, const struct qstr *name)
{
	struct dentry *dentry = __d_alloc(parent->d_sb, name);
	if (!dentry)
		return NULL;
	spin_lock(&parent->d_lock);
	
	__dget_dlock(parent);
	dentry->d_parent = parent;
	list_add(&dentry->d_child, &parent->d_subdirs);
	spin_unlock(&parent->d_lock);

	return dentry;
}

struct dentry *d_alloc_anon(struct super_block *sb)
{
	return __d_alloc(sb, NULL);
}

struct dentry *d_alloc_cursor(struct dentry * parent)
{
	struct dentry *dentry = d_alloc_anon(parent->d_sb);
	if (dentry) {
		dentry->d_flags |= DCACHE_DENTRY_CURSOR;
		dentry->d_parent = dget(parent);
	}
	return dentry;
}

struct dentry *d_alloc_pseudo(struct super_block *sb, const struct qstr *name)
{
	struct dentry *dentry = __d_alloc(sb, name);
	if (likely(dentry))
		dentry->d_flags |= DCACHE_NORCU;
	return dentry;
}

struct dentry *d_alloc_name(struct dentry *parent, const char *name)
{
	struct qstr q;

	q.name = name;
	q.hash_len = hashlen_string(parent, name);
	return d_alloc(parent, &q);
}

void d_set_d_op(struct dentry *dentry, const struct dentry_operations *op)
{
	WARN_ON_ONCE(dentry->d_op);
	WARN_ON_ONCE(dentry->d_flags & (DCACHE_OP_HASH	|
				DCACHE_OP_COMPARE	|
				DCACHE_OP_REVALIDATE	|
				DCACHE_OP_WEAK_REVALIDATE	|
				DCACHE_OP_DELETE	|
				DCACHE_OP_REAL));
	dentry->d_op = op;
	if (!op)
		return;
	if (op->d_hash)
		dentry->d_flags |= DCACHE_OP_HASH;
	if (op->d_compare)
		dentry->d_flags |= DCACHE_OP_COMPARE;
	if (op->d_revalidate)
		dentry->d_flags |= DCACHE_OP_REVALIDATE;
	if (op->d_weak_revalidate)
		dentry->d_flags |= DCACHE_OP_WEAK_REVALIDATE;
	if (op->d_delete)
		dentry->d_flags |= DCACHE_OP_DELETE;
	if (op->d_prune)
		dentry->d_flags |= DCACHE_OP_PRUNE;
	if (op->d_real)
		dentry->d_flags |= DCACHE_OP_REAL;

}

void d_set_fallthru(struct dentry *dentry)
{
	spin_lock(&dentry->d_lock);
	dentry->d_flags |= DCACHE_FALLTHRU;
	spin_unlock(&dentry->d_lock);
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

	if (unlikely(!(inode->i_opflags & IOP_NOFOLLOW))) {
		if (unlikely(inode->i_op->get_link)) {
			add_flags = DCACHE_SYMLINK_TYPE;
			goto type_determined;
		}
		inode->i_opflags |= IOP_NOFOLLOW;
	}

	if (unlikely(!S_ISREG(inode->i_mode)))
		add_flags = DCACHE_SPECIAL_TYPE;

type_determined:
	if (unlikely(IS_AUTOMOUNT(inode)))
		add_flags |= DCACHE_NEED_AUTOMOUNT;
	return add_flags;
}

static void __d_instantiate(struct dentry *dentry, struct inode *inode)
{
	unsigned add_flags = d_flags_for_inode(inode);
	WARN_ON(d_in_lookup(dentry));

	spin_lock(&dentry->d_lock);
	
	if (dentry->d_flags & DCACHE_LRU_LIST)
		this_cpu_dec(nr_dentry_negative);
	hlist_add_head(&dentry->d_u.d_alias, &inode->i_dentry);
	raw_write_seqcount_begin(&dentry->d_seq);
	__d_set_inode_and_type(dentry, inode, add_flags);
	raw_write_seqcount_end(&dentry->d_seq);
	fsnotify_update_flags(dentry);
	spin_unlock(&dentry->d_lock);
}

 
void d_instantiate(struct dentry *entry, struct inode * inode)
{
	BUG_ON(!hlist_unhashed(&entry->d_u.d_alias));
	if (inode) {
		security_d_instantiate(entry, inode);
		spin_lock(&inode->i_lock);
		__d_instantiate(entry, inode);
		spin_unlock(&inode->i_lock);
	}
}

void d_instantiate_new(struct dentry *entry, struct inode *inode)
{
	BUG_ON(!hlist_unhashed(&entry->d_u.d_alias));
	BUG_ON(!inode);
	lockdep_annotate_inode_mutex_key(inode);
	security_d_instantiate(entry, inode);
	spin_lock(&inode->i_lock);
	__d_instantiate(entry, inode);
	WARN_ON(!(inode->i_state & I_NEW));
	inode->i_state &= ~I_NEW & ~I_CREATING;
	smp_mb();
	wake_up_bit(&inode->i_state, __I_NEW);
	spin_unlock(&inode->i_lock);
}

struct dentry *d_make_root(struct inode *root_inode)
{
	struct dentry *res = NULL;

	if (root_inode) {
		res = d_alloc_anon(root_inode->i_sb);
		if (res)
			d_instantiate(res, root_inode);
		else
			iput(root_inode);
	}
	return res;
}

static struct dentry *__d_instantiate_anon(struct dentry *dentry,
					   struct inode *inode,
					   bool disconnected)
{
	struct dentry *res;
	unsigned add_flags;

	security_d_instantiate(dentry, inode);
	spin_lock(&inode->i_lock);
	res = __d_find_any_alias(inode);
	if (res) {
		spin_unlock(&inode->i_lock);
		dput(dentry);
		goto out_iput;
	}

	
	add_flags = d_flags_for_inode(inode);

	if (disconnected)
		add_flags |= DCACHE_DISCONNECTED;

	spin_lock(&dentry->d_lock);
	__d_set_inode_and_type(dentry, inode, add_flags);
	hlist_add_head(&dentry->d_u.d_alias, &inode->i_dentry);
	if (!disconnected) {
		hlist_bl_lock(&dentry->d_sb->s_roots);
		hlist_bl_add_head(&dentry->d_hash, &dentry->d_sb->s_roots);
		hlist_bl_unlock(&dentry->d_sb->s_roots);
	}
	spin_unlock(&dentry->d_lock);
	spin_unlock(&inode->i_lock);

	return dentry;

 out_iput:
	iput(inode);
	return res;
}

struct dentry *d_instantiate_anon(struct dentry *dentry, struct inode *inode)
{
	return __d_instantiate_anon(dentry, inode, true);
}

static struct dentry *__d_obtain_alias(struct inode *inode, bool disconnected)
{
	struct dentry *tmp;
	struct dentry *res;

	if (!inode)
		return ERR_PTR(-ESTALE);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	res = d_find_any_alias(inode);
	if (res)
		goto out_iput;

	tmp = d_alloc_anon(inode->i_sb);
	if (!tmp) {
		res = ERR_PTR(-ENOMEM);
		goto out_iput;
	}

	return __d_instantiate_anon(tmp, inode, disconnected);

out_iput:
	iput(inode);
	return res;
}

struct dentry *d_obtain_alias(struct inode *inode)
{
	return __d_obtain_alias(inode, true);
}

struct dentry *d_obtain_root(struct inode *inode)
{
	return __d_obtain_alias(inode, false);
}

struct dentry *d_add_ci(struct dentry *dentry, struct inode *inode,
			struct qstr *name)
{
	struct dentry *found, *res;

	
	found = d_hash_and_lookup(dentry->d_parent, name);
	if (found) {
		iput(inode);
		return found;
	}
	if (d_in_lookup(dentry)) {
		found = d_alloc_parallel(dentry->d_parent, name,
					dentry->d_wait);
		if (IS_ERR(found) || !d_in_lookup(found)) {
			iput(inode);
			return found;
		}
	} else {
		found = d_alloc(dentry->d_parent, name);
		if (!found) {
			iput(inode);
			return ERR_PTR(-ENOMEM);
		} 
	}
	res = d_splice_alias(inode, found);
	if (res) {
		dput(found);
		return res;
	}
	return found;
}

static inline bool d_same_name(const struct dentry *dentry,
				const struct dentry *parent,
				const struct qstr *name)
{
	if (likely(!(parent->d_flags & DCACHE_OP_COMPARE))) {
		if (dentry->d_name.len != name->len)
			return false;
		return dentry_cmp(dentry, name->name, name->len) == 0;
	}
	return parent->d_op->d_compare(dentry,
				       dentry->d_name.len, dentry->d_name.name,
				       name) == 0;
}

struct dentry *__d_lookup_rcu(const struct dentry *parent,
				const struct qstr *name,
				unsigned *seqp)
{
	u64 hashlen = name->hash_len;
	const unsigned char *str = name->name;
	struct hlist_bl_head *b = d_hash(hashlen_hash(hashlen));
	struct hlist_bl_node *node;
	struct dentry *dentry;

	

	
	hlist_bl_for_each_entry_rcu(dentry, node, b, d_hash) {
		unsigned seq;

seqretry:
		
		seq = raw_seqcount_begin(&dentry->d_seq);
		if (dentry->d_parent != parent)
			continue;
		if (d_unhashed(dentry))
			continue;

		if (unlikely(parent->d_flags & DCACHE_OP_COMPARE)) {
			int tlen;
			const char *tname;
			if (dentry->d_name.hash != hashlen_hash(hashlen))
				continue;
			tlen = dentry->d_name.len;
			tname = dentry->d_name.name;
			
			if (read_seqcount_retry(&dentry->d_seq, seq)) {
				cpu_relax();
				goto seqretry;
			}
			if (parent->d_op->d_compare(dentry,
						    tlen, tname, name) != 0)
				continue;
		} else {
			if (dentry->d_name.hash_len != hashlen)
				continue;
			if (dentry_cmp(dentry, str, hashlen_len(hashlen)) != 0)
				continue;
		}
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

struct dentry *d_hash_and_lookup(struct dentry *dir, struct qstr *name)
{
	
	name->hash = full_name_hash(dir, name->name, name->len);
	if (dir->d_flags & DCACHE_OP_HASH) {
		int err = dir->d_op->d_hash(dir, name);
		if (unlikely(err < 0))
			return ERR_PTR(err);
	}
	return d_lookup(dir, name);
}

 

 
void d_delete(struct dentry * dentry)
{
	struct inode *inode = dentry->d_inode;

	spin_lock(&inode->i_lock);
	spin_lock(&dentry->d_lock);
	
	if (dentry->d_lockref.count == 1) {
		dentry->d_flags &= ~DCACHE_CANT_MOUNT;
		dentry_unlink_inode(dentry);
	} else {
		__d_drop(dentry);
		spin_unlock(&dentry->d_lock);
		spin_unlock(&inode->i_lock);
	}
}

static void __d_rehash(struct dentry *entry)
{
	struct hlist_bl_head *b = d_hash(entry->d_name.hash);

	hlist_bl_lock(b);
	hlist_bl_add_head_rcu(&entry->d_hash, b);
	hlist_bl_unlock(b);
}

 
void d_rehash(struct dentry * entry)
{
	spin_lock(&entry->d_lock);
	__d_rehash(entry);
	spin_unlock(&entry->d_lock);
}

static inline unsigned start_dir_add(struct inode *dir)
{

	for (;;) {
		unsigned n = dir->i_dir_seq;
		if (!(n & 1) && cmpxchg(&dir->i_dir_seq, n, n + 1) == n)
			return n;
		cpu_relax();
	}
}

static inline void end_dir_add(struct inode *dir, unsigned n)
{
	smp_store_release(&dir->i_dir_seq, n + 2);
}

static void d_wait_lookup(struct dentry *dentry)
{
	if (d_in_lookup(dentry)) {
		DECLARE_WAITQUEUE(wait, current);
		add_wait_queue(dentry->d_wait, &wait);
		do {
			set_current_state(TASK_UNINTERRUPTIBLE);
			spin_unlock(&dentry->d_lock);
			schedule();
			spin_lock(&dentry->d_lock);
		} while (d_in_lookup(dentry));
	}
}

struct dentry *d_alloc_parallel(struct dentry *parent,
				const struct qstr *name,
				wait_queue_head_t *wq)
{
	unsigned int hash = name->hash;
	struct hlist_bl_head *b = in_lookup_hash(parent, hash);
	struct hlist_bl_node *node;
	struct dentry *new = d_alloc(parent, name);
	struct dentry *dentry;
	unsigned seq, r_seq, d_seq;

	if (unlikely(!new))
		return ERR_PTR(-ENOMEM);

retry:
	rcu_read_lock();
	seq = smp_load_acquire(&parent->d_inode->i_dir_seq);
	r_seq = read_seqbegin(&rename_lock);
	dentry = __d_lookup_rcu(parent, name, &d_seq);
	if (unlikely(dentry)) {
		if (!lockref_get_not_dead(&dentry->d_lockref)) {
			rcu_read_unlock();
			goto retry;
		}
		if (read_seqcount_retry(&dentry->d_seq, d_seq)) {
			rcu_read_unlock();
			dput(dentry);
			goto retry;
		}
		rcu_read_unlock();
		dput(new);
		return dentry;
	}
	if (unlikely(read_seqretry(&rename_lock, r_seq))) {
		rcu_read_unlock();
		goto retry;
	}

	if (unlikely(seq & 1)) {
		rcu_read_unlock();
		goto retry;
	}

	hlist_bl_lock(b);
	if (unlikely(READ_ONCE(parent->d_inode->i_dir_seq) != seq)) {
		hlist_bl_unlock(b);
		rcu_read_unlock();
		goto retry;
	}
	
	hlist_bl_for_each_entry(dentry, node, b, d_u.d_in_lookup_hash) {
		if (dentry->d_name.hash != hash)
			continue;
		if (dentry->d_parent != parent)
			continue;
		if (!d_same_name(dentry, parent, name))
			continue;
		hlist_bl_unlock(b);
		
		if (!lockref_get_not_dead(&dentry->d_lockref)) {
			rcu_read_unlock();
			goto retry;
		}

		rcu_read_unlock();
		
		spin_lock(&dentry->d_lock);
		d_wait_lookup(dentry);
		
		if (unlikely(dentry->d_name.hash != hash))
			goto mismatch;
		if (unlikely(dentry->d_parent != parent))
			goto mismatch;
		if (unlikely(d_unhashed(dentry)))
			goto mismatch;
		if (unlikely(!d_same_name(dentry, parent, name)))
			goto mismatch;
		
		spin_unlock(&dentry->d_lock);
		dput(new);
		return dentry;
	}
	rcu_read_unlock();
	
	new->d_flags |= DCACHE_PAR_LOOKUP;
	new->d_wait = wq;
	hlist_bl_add_head_rcu(&new->d_u.d_in_lookup_hash, b);
	hlist_bl_unlock(b);
	return new;
mismatch:
	spin_unlock(&dentry->d_lock);
	dput(dentry);
	goto retry;
}

void __d_lookup_done(struct dentry *dentry)
{
	struct hlist_bl_head *b = in_lookup_hash(dentry->d_parent,
						 dentry->d_name.hash);
	hlist_bl_lock(b);
	dentry->d_flags &= ~DCACHE_PAR_LOOKUP;
	__hlist_bl_del(&dentry->d_u.d_in_lookup_hash);
	wake_up_all(dentry->d_wait);
	dentry->d_wait = NULL;
	hlist_bl_unlock(b);
	INIT_HLIST_NODE(&dentry->d_u.d_alias);
	INIT_LIST_HEAD(&dentry->d_lru);
}

static inline void __d_add(struct dentry *dentry, struct inode *inode)
{
	struct inode *dir = NULL;
	unsigned n;
	spin_lock(&dentry->d_lock);
	if (unlikely(d_in_lookup(dentry))) {
		dir = dentry->d_parent->d_inode;
		n = start_dir_add(dir);
		__d_lookup_done(dentry);
	}
	if (inode) {
		unsigned add_flags = d_flags_for_inode(inode);
		hlist_add_head(&dentry->d_u.d_alias, &inode->i_dentry);
		raw_write_seqcount_begin(&dentry->d_seq);
		__d_set_inode_and_type(dentry, inode, add_flags);
		raw_write_seqcount_end(&dentry->d_seq);
		fsnotify_update_flags(dentry);
	}
	__d_rehash(dentry);
	if (dir)
		end_dir_add(dir, n);
	spin_unlock(&dentry->d_lock);
	if (inode)
		spin_unlock(&inode->i_lock);
}

void d_add(struct dentry *entry, struct inode *inode)
{
	if (inode) {
		security_d_instantiate(entry, inode);
		spin_lock(&inode->i_lock);
	}
	__d_add(entry, inode);
}

struct dentry *d_exact_alias(struct dentry *entry, struct inode *inode)
{
	struct dentry *alias;
	unsigned int hash = entry->d_name.hash;

	spin_lock(&inode->i_lock);
	hlist_for_each_entry(alias, &inode->i_dentry, d_u.d_alias) {
		
		if (alias->d_name.hash != hash)
			continue;
		if (alias->d_parent != entry->d_parent)
			continue;
		if (!d_same_name(alias, entry->d_parent, &entry->d_name))
			continue;
		spin_lock(&alias->d_lock);
		if (!d_unhashed(alias)) {
			spin_unlock(&alias->d_lock);
			alias = NULL;
		} else {
			__dget_dlock(alias);
			__d_rehash(alias);
			spin_unlock(&alias->d_lock);
		}
		spin_unlock(&inode->i_lock);
		return alias;
	}
	spin_unlock(&inode->i_lock);
	return NULL;
}

static void swap_names(struct dentry *dentry, struct dentry *target)
{
	if (unlikely(dname_external(target))) {
		if (unlikely(dname_external(dentry))) {
			
			swap(target->d_name.name, dentry->d_name.name);
		} else {
			
			memcpy(target->d_iname, dentry->d_name.name,
					dentry->d_name.len + 1);
			dentry->d_name.name = target->d_name.name;
			target->d_name.name = target->d_iname;
		}
	} else {
		if (unlikely(dname_external(dentry))) {
			
			memcpy(dentry->d_iname, target->d_name.name,
					target->d_name.len + 1);
			target->d_name.name = dentry->d_name.name;
			dentry->d_name.name = dentry->d_iname;
		} else {
			
			unsigned int i;
			BUILD_BUG_ON(!IS_ALIGNED(DNAME_INLINE_LEN, sizeof(long)));
			for (i = 0; i < DNAME_INLINE_LEN / sizeof(long); i++) {
				swap(((long *) &dentry->d_iname)[i],
				     ((long *) &target->d_iname)[i]);
			}
		}
	}
	swap(dentry->d_name.hash_len, target->d_name.hash_len);
}

static void copy_name(struct dentry *dentry, struct dentry *target)
{
	struct external_name *old_name = NULL;
	if (unlikely(dname_external(dentry)))
		old_name = external_name(dentry);
	if (unlikely(dname_external(target))) {
		atomic_inc(&external_name(target)->u.count);
		dentry->d_name = target->d_name;
	} else {
		memcpy(dentry->d_iname, target->d_name.name,
				target->d_name.len + 1);
		dentry->d_name.name = dentry->d_iname;
		dentry->d_name.hash_len = target->d_name.hash_len;
	}
	if (old_name && likely(atomic_dec_and_test(&old_name->u.count)))
		kfree_rcu(old_name, u.head);
}

static void __d_move(struct dentry *dentry, struct dentry *target,
		     bool exchange)
{
	struct dentry *old_parent, *p;
	struct inode *dir = NULL;
	unsigned n;

	WARN_ON(!dentry->d_inode);
	if (WARN_ON(dentry == target))
		return;

	BUG_ON(d_ancestor(target, dentry));
	old_parent = dentry->d_parent;
	p = d_ancestor(old_parent, target);
	if (IS_ROOT(dentry)) {
		BUG_ON(p);
		spin_lock(&target->d_parent->d_lock);
	} else if (!p) {
		
		spin_lock(&target->d_parent->d_lock);
		spin_lock_nested(&old_parent->d_lock, DENTRY_D_LOCK_NESTED);
	} else {
		BUG_ON(p == dentry);
		spin_lock(&old_parent->d_lock);
		if (p != target)
			spin_lock_nested(&target->d_parent->d_lock,
					DENTRY_D_LOCK_NESTED);
	}
	spin_lock_nested(&dentry->d_lock, 2);
	spin_lock_nested(&target->d_lock, 3);

	if (unlikely(d_in_lookup(target))) {
		dir = target->d_parent->d_inode;
		n = start_dir_add(dir);
		__d_lookup_done(target);
	}

	write_seqcount_begin(&dentry->d_seq);
	write_seqcount_begin_nested(&target->d_seq, DENTRY_D_LOCK_NESTED);

	
	if (!d_unhashed(dentry))
		___d_drop(dentry);
	if (!d_unhashed(target))
		___d_drop(target);

	
	dentry->d_parent = target->d_parent;
	if (!exchange) {
		copy_name(dentry, target);
		target->d_hash.pprev = NULL;
		dentry->d_parent->d_lockref.count++;
		if (dentry != old_parent) 
			WARN_ON(!--old_parent->d_lockref.count);
	} else {
		target->d_parent = old_parent;
		swap_names(dentry, target);
		list_move(&target->d_child, &target->d_parent->d_subdirs);
		__d_rehash(target);
		fsnotify_update_flags(target);
	}
	list_move(&dentry->d_child, &dentry->d_parent->d_subdirs);
	__d_rehash(dentry);
	fsnotify_update_flags(dentry);
	fscrypt_handle_d_move(dentry);

	write_seqcount_end(&target->d_seq);
	write_seqcount_end(&dentry->d_seq);

	if (dir)
		end_dir_add(dir, n);

	if (dentry->d_parent != old_parent)
		spin_unlock(&dentry->d_parent->d_lock);
	if (dentry != old_parent)
		spin_unlock(&old_parent->d_lock);
	spin_unlock(&target->d_lock);
	spin_unlock(&dentry->d_lock);
}

void d_move(struct dentry *dentry, struct dentry *target)
{
	write_seqlock(&rename_lock);
	__d_move(dentry, target, false);
	write_sequnlock(&rename_lock);
}

void d_exchange(struct dentry *dentry1, struct dentry *dentry2)
{
	write_seqlock(&rename_lock);

	WARN_ON(!dentry1->d_inode);
	WARN_ON(!dentry2->d_inode);
	WARN_ON(IS_ROOT(dentry1));
	WARN_ON(IS_ROOT(dentry2));

	__d_move(dentry1, dentry2, true);

	write_sequnlock(&rename_lock);
}

struct dentry *d_ancestor(struct dentry *p1, struct dentry *p2)
{
	struct dentry *p;

	for (p = p2; !IS_ROOT(p); p = p->d_parent) {
		if (p->d_parent == p1)
			return p;
	}
	return NULL;
}

static int __d_unalias(struct inode *inode,
		struct dentry *dentry, struct dentry *alias)
{
	struct mutex *m1 = NULL;
	struct rw_semaphore *m2 = NULL;
	int ret = -ESTALE;

	
	if (alias->d_parent == dentry->d_parent)
		goto out_unalias;

	
	if (!mutex_trylock(&dentry->d_sb->s_vfs_rename_mutex))
		goto out_err;
	m1 = &dentry->d_sb->s_vfs_rename_mutex;
	if (!inode_trylock_shared(alias->d_parent->d_inode))
		goto out_err;
	m2 = &alias->d_parent->d_inode->i_rwsem;
out_unalias:
	__d_move(alias, dentry, false);
	ret = 0;
out_err:
	if (m2)
		up_read(m2);
	if (m1)
		mutex_unlock(m1);
	return ret;
}

struct dentry *d_splice_alias(struct inode *inode, struct dentry *dentry)
{
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	BUG_ON(!d_unhashed(dentry));

	if (!inode)
		goto out;

	security_d_instantiate(dentry, inode);
	spin_lock(&inode->i_lock);
	if (S_ISDIR(inode->i_mode)) {
		struct dentry *new = __d_find_any_alias(inode);
		if (unlikely(new)) {
			
			spin_unlock(&inode->i_lock);
			write_seqlock(&rename_lock);
			if (unlikely(d_ancestor(new, dentry))) {
				write_sequnlock(&rename_lock);
				dput(new);
				new = ERR_PTR(-ELOOP);
				pr_warn_ratelimited(
					"VFS: Lookup of '%s' in %s %s"
					" would have caused loop\n",
					dentry->d_name.name,
					inode->i_sb->s_type->name,
					inode->i_sb->s_id);
			} else if (!IS_ROOT(new)) {
				struct dentry *old_parent = dget(new->d_parent);
				int err = __d_unalias(inode, dentry, new);
				write_sequnlock(&rename_lock);
				if (err) {
					dput(new);
					new = ERR_PTR(err);
				}
				dput(old_parent);
			} else {
				__d_move(new, dentry, false);
				write_sequnlock(&rename_lock);
			}
			iput(inode);
			return new;
		}
	}
out:
	__d_add(dentry, inode);
	return NULL;
}

  
bool is_subdir(struct dentry *new_dentry, struct dentry *old_dentry)
{
	bool result;
	unsigned seq;

	if (new_dentry == old_dentry)
		return true;

	do {
		
		seq = read_seqbegin(&rename_lock);
		
		rcu_read_lock();
		if (d_ancestor(old_dentry, new_dentry))
			result = true;
		else
			result = false;
		rcu_read_unlock();
	} while (read_seqretry(&rename_lock, seq));

	return result;
}

static enum d_walk_ret d_genocide_kill(void *data, struct dentry *dentry)
{
	struct dentry *root = data;
	if (dentry != root) {
		if (d_unhashed(dentry) || !dentry->d_inode)
			return D_WALK_SKIP;

		if (!(dentry->d_flags & DCACHE_GENOCIDE)) {
			dentry->d_flags |= DCACHE_GENOCIDE;
			dentry->d_lockref.count--;
		}
	}
	return D_WALK_CONTINUE;
}

void d_genocide(struct dentry *parent)
{
	d_walk(parent, parent, d_genocide_kill);
}


void d_tmpfile(struct dentry *dentry, struct inode *inode)
{
	inode_dec_link_count(inode);
	BUG_ON(dentry->d_name.name != dentry->d_iname ||
		!hlist_unhashed(&dentry->d_u.d_alias) ||
		!d_unlinked(dentry));
	spin_lock(&dentry->d_parent->d_lock);
	spin_lock_nested(&dentry->d_lock, DENTRY_D_LOCK_NESTED);
	dentry->d_name.len = sprintf(dentry->d_iname, "#%llu",
				(unsigned long long)inode->i_ino);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&dentry->d_parent->d_lock);
	d_instantiate(dentry, inode);
}

static __initdata unsigned long dhash_entries;
static int __init set_dhash_entries(char *str)
{
	if (!str)
		return 0;
	dhash_entries = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("dhash_entries=", set_dhash_entries);

static void __init dcache_init_early(void)
{
	
	if (hashdist)
		return;

	dentry_hashtable =
		alloc_large_system_hash("Dentry cache",
					sizeof(struct hlist_bl_head),
					dhash_entries,
					13,
					HASH_EARLY | HASH_ZERO,
					&d_hash_shift,
					NULL,
					0,
					0);
	d_hash_shift = 32 - d_hash_shift;
}

static void __init dcache_init(void)
{
	
	dentry_cache = KMEM_CACHE_USERCOPY(dentry,
		SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|SLAB_MEM_SPREAD|SLAB_ACCOUNT,
		d_iname);

	
	if (!hashdist)
		return;

	dentry_hashtable =
		alloc_large_system_hash("Dentry cache",
					sizeof(struct hlist_bl_head),
					dhash_entries,
					13,
					HASH_ZERO,
					&d_hash_shift,
					NULL,
					0,
					0);
	d_hash_shift = 32 - d_hash_shift;
}

struct kmem_cache *names_cachep __read_mostly;

void __init vfs_caches_init_early(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(in_lookup_hashtable); i++)
		INIT_HLIST_BL_HEAD(&in_lookup_hashtable[i]);

	dcache_init_early();
	inode_init_early();
}

void __init vfs_caches_init(void)
{
	names_cachep = kmem_cache_create_usercopy("names_cache", PATH_MAX, 0,
			SLAB_HWCACHE_ALIGN|SLAB_PANIC, 0, PATH_MAX, NULL);

	dcache_init();
	inode_init();
	files_init();
	files_maxfiles_init();
	mnt_init();
	bdev_cache_init();
	chrdev_init();
}
