
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/backing-dev.h>
#include <linux/hash.h>
#include <linux/swap.h>
#include <linux/security.h>
#include <linux/cdev.h>
#include <linux/memblock.h>
#include <linux/fsnotify.h>
#include <linux/mount.h>
#include <linux/tracepoint.h>

#include <linux/buffer_head.h> 
#include <linux/ratelimit.h>
#include <linux/list_lru.h>
#include <linux/iversion.h>
#include "internal.h"

static unsigned int i_hash_mask __read_mostly;
static unsigned int i_hash_shift __read_mostly;
static struct hlist_head *inode_hashtable __read_mostly;
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(inode_hash_lock);

const struct address_space_operations empty_aops = {
};

static DEFINE_PER_CPU(unsigned long, nr_inodes);
static DEFINE_PER_CPU(unsigned long, nr_unused);

static struct kmem_cache *inode_cachep __read_mostly;

static long get_nr_inodes(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_inodes, i);
	return sum < 0 ? 0 : sum;
}

static inline long get_nr_inodes_unused(void)
{
	int i;
	long sum = 0;
	for_each_possible_cpu(i)
		sum += per_cpu(nr_unused, i);
	return sum < 0 ? 0 : sum;
}

/* Stubbed - not used externally */
long get_nr_dirty_inodes(void)
{
	return 0;
}

static int no_open(struct inode *inode, struct file *file)
{
	return -ENXIO;
}

int inode_init_always(struct super_block *sb, struct inode *inode)
{
	static const struct inode_operations empty_iops;
	static const struct file_operations no_open_fops = {.open = no_open};
	struct address_space *const mapping = &inode->i_data;

	inode->i_sb = sb;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_flags = 0;
	atomic64_set(&inode->i_sequence, 0);
	atomic_set(&inode->i_count, 1);
	inode->i_op = &empty_iops;
	inode->i_fop = &no_open_fops;
	inode->i_ino = 0;
	inode->__i_nlink = 1;
	inode->i_opflags = 0;
	if (sb->s_xattr)
		inode->i_opflags |= IOP_XATTR;
	i_uid_write(inode, 0);
	i_gid_write(inode, 0);
	atomic_set(&inode->i_writecount, 0);
	inode->i_size = 0;
	inode->i_write_hint = WRITE_LIFE_NOT_SET;
	inode->i_blocks = 0;
	inode->i_bytes = 0;
	inode->i_generation = 0;
	inode->i_pipe = NULL;
	inode->i_cdev = NULL;
	inode->i_link = NULL;
	inode->i_dir_seq = 0;
	inode->i_rdev = 0;
	inode->dirtied_when = 0;

	if (security_inode_alloc(inode))
		goto out;
	spin_lock_init(&inode->i_lock);
	lockdep_set_class(&inode->i_lock, &sb->s_type->i_lock_key);

	init_rwsem(&inode->i_rwsem);
	lockdep_set_class(&inode->i_rwsem, &sb->s_type->i_mutex_key);

	atomic_set(&inode->i_dio_count, 0);

	mapping->a_ops = &empty_aops;
	mapping->host = inode;
	mapping->flags = 0;
	mapping->wb_err = 0;
	atomic_set(&mapping->i_mmap_writable, 0);
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	mapping->private_data = NULL;
	mapping->writeback_index = 0;
	init_rwsem(&mapping->invalidate_lock);
	lockdep_set_class_and_name(&mapping->invalidate_lock,
				   &sb->s_type->invalidate_lock_key,
				   "mapping.invalidate_lock");
	inode->i_private = NULL;
	inode->i_mapping = mapping;
	INIT_HLIST_HEAD(&inode->i_dentry);	

	inode->i_flctx = NULL;
	this_cpu_inc(nr_inodes);

	return 0;
out:
	return -ENOMEM;
}

void free_inode_nonrcu(struct inode *inode)
{
	kmem_cache_free(inode_cachep, inode);
}

static void i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	if (inode->free_inode)
		inode->free_inode(inode);
	else
		free_inode_nonrcu(inode);
}

static struct inode *alloc_inode(struct super_block *sb)
{
	const struct super_operations *ops = sb->s_op;
	struct inode *inode;

	if (ops->alloc_inode)
		inode = ops->alloc_inode(sb);
	else
		inode = alloc_inode_sb(sb, inode_cachep, GFP_KERNEL);

	if (!inode)
		return NULL;

	if (unlikely(inode_init_always(sb, inode))) {
		if (ops->destroy_inode) {
			ops->destroy_inode(inode);
			if (!ops->free_inode)
				return NULL;
		}
		inode->free_inode = ops->free_inode;
		i_callback(&inode->i_rcu);
		return NULL;
	}

	return inode;
}

void __destroy_inode(struct inode *inode)
{
	BUG_ON(inode_has_buffers(inode));
	inode_detach_wb(inode);
	security_inode_free(inode);
	fsnotify_inode_delete(inode);
	locks_free_lock_context(inode);
	if (!inode->i_nlink) {
		WARN_ON(atomic_long_read(&inode->i_sb->s_remove_count) == 0);
		atomic_long_dec(&inode->i_sb->s_remove_count);
	}

	this_cpu_dec(nr_inodes);
}

static void destroy_inode(struct inode *inode)
{
	const struct super_operations *ops = inode->i_sb->s_op;

	BUG_ON(!list_empty(&inode->i_lru));
	__destroy_inode(inode);
	if (ops->destroy_inode) {
		ops->destroy_inode(inode);
		if (!ops->free_inode)
			return;
	}
	inode->free_inode = ops->free_inode;
	call_rcu(&inode->i_rcu, i_callback);
}

void drop_nlink(struct inode *inode)
{
	WARN_ON(inode->i_nlink == 0);
	inode->__i_nlink--;
	if (!inode->i_nlink)
		atomic_long_inc(&inode->i_sb->s_remove_count);
}

void clear_nlink(struct inode *inode)
{
	if (inode->i_nlink) {
		inode->__i_nlink = 0;
		atomic_long_inc(&inode->i_sb->s_remove_count);
	}
}

void set_nlink(struct inode *inode, unsigned int nlink)
{
	if (!nlink) {
		clear_nlink(inode);
	} else {
		
		if (inode->i_nlink == 0)
			atomic_long_dec(&inode->i_sb->s_remove_count);

		inode->__i_nlink = nlink;
	}
}

void inc_nlink(struct inode *inode)
{
	if (unlikely(inode->i_nlink == 0)) {
		WARN_ON(!(inode->i_state & I_LINKABLE));
		atomic_long_dec(&inode->i_sb->s_remove_count);
	}

	inode->__i_nlink++;
}

static void __address_space_init_once(struct address_space *mapping)
{
	xa_init_flags(&mapping->i_pages, XA_FLAGS_LOCK_IRQ | XA_FLAGS_ACCOUNT);
	init_rwsem(&mapping->i_mmap_rwsem);
	INIT_LIST_HEAD(&mapping->private_list);
	spin_lock_init(&mapping->private_lock);
	mapping->i_mmap = RB_ROOT_CACHED;
}

/* Stubbed - not used externally */
void address_space_init_once(struct address_space *mapping)
{
}

void inode_init_once(struct inode *inode)
{
	memset(inode, 0, sizeof(*inode));
	INIT_HLIST_NODE(&inode->i_hash);
	INIT_LIST_HEAD(&inode->i_devices);
	INIT_LIST_HEAD(&inode->i_io_list);
	INIT_LIST_HEAD(&inode->i_wb_list);
	INIT_LIST_HEAD(&inode->i_lru);
	__address_space_init_once(&inode->i_data);
	i_size_ordered_init(inode);
}

static void init_once(void *foo)
{
	struct inode *inode = (struct inode *) foo;

	inode_init_once(inode);
}

void __iget(struct inode *inode)
{
	atomic_inc(&inode->i_count);
}

void ihold(struct inode *inode)
{
	WARN_ON(atomic_inc_return(&inode->i_count) < 2);
}

static void __inode_add_lru(struct inode *inode, bool rotate)
{
	if (inode->i_state & (I_DIRTY_ALL | I_SYNC | I_FREEING | I_WILL_FREE))
		return;
	if (atomic_read(&inode->i_count))
		return;
	if (!(inode->i_sb->s_flags & SB_ACTIVE))
		return;
	if (!mapping_shrinkable(&inode->i_data))
		return;

	if (list_lru_add(&inode->i_sb->s_inode_lru, &inode->i_lru))
		this_cpu_inc(nr_unused);
	else if (rotate)
		inode->i_state |= I_REFERENCED;
}

void inode_add_lru(struct inode *inode)
{
	__inode_add_lru(inode, false);
}

static void inode_lru_list_del(struct inode *inode)
{
	if (list_lru_del(&inode->i_sb->s_inode_lru, &inode->i_lru))
		this_cpu_dec(nr_unused);
}

void inode_sb_list_add(struct inode *inode)
{
	spin_lock(&inode->i_sb->s_inode_list_lock);
	list_add(&inode->i_sb_list, &inode->i_sb->s_inodes);
	spin_unlock(&inode->i_sb->s_inode_list_lock);
}

static inline void inode_sb_list_del(struct inode *inode)
{
	if (!list_empty(&inode->i_sb_list)) {
		spin_lock(&inode->i_sb->s_inode_list_lock);
		list_del_init(&inode->i_sb_list);
		spin_unlock(&inode->i_sb->s_inode_list_lock);
	}
}

static unsigned long hash(struct super_block *sb, unsigned long hashval)
{
	unsigned long tmp;

	tmp = (hashval * (unsigned long)sb) ^ (GOLDEN_RATIO_PRIME + hashval) /
			L1_CACHE_BYTES;
	tmp = tmp ^ ((tmp ^ GOLDEN_RATIO_PRIME) >> i_hash_shift);
	return tmp & i_hash_mask;
}

void __insert_inode_hash(struct inode *inode, unsigned long hashval)
{
	struct hlist_head *b = inode_hashtable + hash(inode->i_sb, hashval);

	spin_lock(&inode_hash_lock);
	spin_lock(&inode->i_lock);
	hlist_add_head_rcu(&inode->i_hash, b);
	spin_unlock(&inode->i_lock);
	spin_unlock(&inode_hash_lock);
}

void __remove_inode_hash(struct inode *inode)
{
	spin_lock(&inode_hash_lock);
	spin_lock(&inode->i_lock);
	hlist_del_init_rcu(&inode->i_hash);
	spin_unlock(&inode->i_lock);
	spin_unlock(&inode_hash_lock);
}

void dump_mapping(const struct address_space *mapping)
{
	
}

void clear_inode(struct inode *inode)
{
	
	xa_lock_irq(&inode->i_data.i_pages);
	BUG_ON(inode->i_data.nrpages);
	
	xa_unlock_irq(&inode->i_data.i_pages);
	BUG_ON(!list_empty(&inode->i_data.private_list));
	BUG_ON(!(inode->i_state & I_FREEING));
	BUG_ON(inode->i_state & I_CLEAR);
	BUG_ON(!list_empty(&inode->i_wb_list));
	
	inode->i_state = I_FREEING | I_CLEAR;
}

static void evict(struct inode *inode)
{
	const struct super_operations *op = inode->i_sb->s_op;

	BUG_ON(!(inode->i_state & I_FREEING));
	BUG_ON(!list_empty(&inode->i_lru));

	if (!list_empty(&inode->i_io_list))
		inode_io_list_del(inode);

	inode_sb_list_del(inode);

	inode_wait_for_writeback(inode);

	if (op->evict_inode) {
		op->evict_inode(inode);
	} else {
		truncate_inode_pages_final(&inode->i_data);
		clear_inode(inode);
	}
	if (S_ISCHR(inode->i_mode) && inode->i_cdev)
		cd_forget(inode);

	remove_inode_hash(inode);

	spin_lock(&inode->i_lock);
	wake_up_bit(&inode->i_state, __I_NEW);
	BUG_ON(inode->i_state != (I_FREEING | I_CLEAR));
	spin_unlock(&inode->i_lock);

	destroy_inode(inode);
}

static void dispose_list(struct list_head *head)
{
	while (!list_empty(head)) {
		struct inode *inode;

		inode = list_first_entry(head, struct inode, i_lru);
		list_del_init(&inode->i_lru);

		evict(inode);
		cond_resched();
	}
}

void evict_inodes(struct super_block *sb)
{
	struct inode *inode, *next;
	LIST_HEAD(dispose);

again:
	spin_lock(&sb->s_inode_list_lock);
	list_for_each_entry_safe(inode, next, &sb->s_inodes, i_sb_list) {
		if (atomic_read(&inode->i_count))
			continue;

		spin_lock(&inode->i_lock);
		if (inode->i_state & (I_NEW | I_FREEING | I_WILL_FREE)) {
			spin_unlock(&inode->i_lock);
			continue;
		}

		inode->i_state |= I_FREEING;
		inode_lru_list_del(inode);
		spin_unlock(&inode->i_lock);
		list_add(&inode->i_lru, &dispose);

		if (need_resched()) {
			spin_unlock(&sb->s_inode_list_lock);
			cond_resched();
			dispose_list(&dispose);
			goto again;
		}
	}
	spin_unlock(&sb->s_inode_list_lock);

	dispose_list(&dispose);
}

/* Stubbed - not used externally */
int invalidate_inodes(struct super_block *sb, bool kill_dirty)
{
	return 0;
}

static enum lru_status inode_lru_isolate(struct list_head *item,
		struct list_lru_one *lru, spinlock_t *lru_lock, void *arg)
{
	/* Stub: simplified inode LRU isolation for minimal kernel */
	struct list_head *freeable = arg;
	struct inode	*inode = container_of(item, struct inode, i_lru);

	if (!spin_trylock(&inode->i_lock))
		return LRU_SKIP;

	if (atomic_read(&inode->i_count) || (inode->i_state & ~I_REFERENCED)) {
		list_lru_isolate(lru, &inode->i_lru);
		spin_unlock(&inode->i_lock);
		this_cpu_dec(nr_unused);
		return LRU_REMOVED;
	}

	inode->i_state |= I_FREEING;
	list_lru_isolate_move(lru, &inode->i_lru, freeable);
	spin_unlock(&inode->i_lock);
	this_cpu_dec(nr_unused);
	return LRU_REMOVED;
}

long prune_icache_sb(struct super_block *sb, struct shrink_control *sc)
{
	LIST_HEAD(freeable);
	long freed;

	freed = list_lru_shrink_walk(&sb->s_inode_lru, sc,
				     inode_lru_isolate, &freeable);
	dispose_list(&freeable);
	return freed;
}

static void __wait_on_freeing_inode(struct inode *inode);

static struct inode *find_inode(struct super_block *sb,
				struct hlist_head *head,
				int (*test)(struct inode *, void *),
				void *data)
{
	struct inode *inode = NULL;

repeat:
	hlist_for_each_entry(inode, head, i_hash) {
		if (inode->i_sb != sb)
			continue;
		if (!test(inode, data))
			continue;
		spin_lock(&inode->i_lock);
		if (inode->i_state & (I_FREEING|I_WILL_FREE)) {
			__wait_on_freeing_inode(inode);
			goto repeat;
		}
		if (unlikely(inode->i_state & I_CREATING)) {
			spin_unlock(&inode->i_lock);
			return ERR_PTR(-ESTALE);
		}
		__iget(inode);
		spin_unlock(&inode->i_lock);
		return inode;
	}
	return NULL;
}

static struct inode *find_inode_fast(struct super_block *sb,
				struct hlist_head *head, unsigned long ino)
{
	struct inode *inode = NULL;

repeat:
	hlist_for_each_entry(inode, head, i_hash) {
		if (inode->i_ino != ino)
			continue;
		if (inode->i_sb != sb)
			continue;
		spin_lock(&inode->i_lock);
		if (inode->i_state & (I_FREEING|I_WILL_FREE)) {
			__wait_on_freeing_inode(inode);
			goto repeat;
		}
		if (unlikely(inode->i_state & I_CREATING)) {
			spin_unlock(&inode->i_lock);
			return ERR_PTR(-ESTALE);
		}
		__iget(inode);
		spin_unlock(&inode->i_lock);
		return inode;
	}
	return NULL;
}

#define LAST_INO_BATCH 1024
static DEFINE_PER_CPU(unsigned int, last_ino);

unsigned int get_next_ino(void)
{
	unsigned int *p = &get_cpu_var(last_ino);
	unsigned int res = *p;

	res++;
	
	if (unlikely(!res))
		res++;
	*p = res;
	put_cpu_var(last_ino);
	return res;
}

struct inode *new_inode_pseudo(struct super_block *sb)
{
	struct inode *inode = alloc_inode(sb);

	if (inode) {
		spin_lock(&inode->i_lock);
		inode->i_state = 0;
		spin_unlock(&inode->i_lock);
		INIT_LIST_HEAD(&inode->i_sb_list);
	}
	return inode;
}

struct inode *new_inode(struct super_block *sb)
{
	struct inode *inode;

	spin_lock_prefetch(&sb->s_inode_list_lock);

	inode = new_inode_pseudo(sb);
	if (inode)
		inode_sb_list_add(inode);
	return inode;
}

void unlock_new_inode(struct inode *inode)
{
	lockdep_annotate_inode_mutex_key(inode);
	spin_lock(&inode->i_lock);
	WARN_ON(!(inode->i_state & I_NEW));
	inode->i_state &= ~I_NEW & ~I_CREATING;
	smp_mb();
	wake_up_bit(&inode->i_state, __I_NEW);
	spin_unlock(&inode->i_lock);
}

/* STUB: discard_new_inode not used externally */
void discard_new_inode(struct inode *inode)
{
	iput(inode);
}

/* Stub: not used in minimal kernel */
void lock_two_nondirectories(struct inode *inode1, struct inode *inode2) { }
void unlock_two_nondirectories(struct inode *inode1, struct inode *inode2) { }

struct inode *inode_insert5(struct inode *inode, unsigned long hashval,
			    int (*test)(struct inode *, void *),
			    int (*set)(struct inode *, void *), void *data)
{
	struct hlist_head *head = inode_hashtable + hash(inode->i_sb, hashval);
	struct inode *old;
	bool creating = inode->i_state & I_CREATING;

again:
	spin_lock(&inode_hash_lock);
	old = find_inode(inode->i_sb, head, test, data);
	if (unlikely(old)) {
		
		spin_unlock(&inode_hash_lock);
		if (IS_ERR(old))
			return NULL;
		wait_on_inode(old);
		if (unlikely(inode_unhashed(old))) {
			iput(old);
			goto again;
		}
		return old;
	}

	if (set && unlikely(set(inode, data))) {
		inode = NULL;
		goto unlock;
	}

	spin_lock(&inode->i_lock);
	inode->i_state |= I_NEW;
	hlist_add_head_rcu(&inode->i_hash, head);
	spin_unlock(&inode->i_lock);
	if (!creating)
		inode_sb_list_add(inode);
unlock:
	spin_unlock(&inode_hash_lock);

	return inode;
}

struct inode *iget5_locked(struct super_block *sb, unsigned long hashval,
		int (*test)(struct inode *, void *),
		int (*set)(struct inode *, void *), void *data)
{
	struct inode *inode = ilookup5(sb, hashval, test, data);

	if (!inode) {
		struct inode *new = alloc_inode(sb);

		if (new) {
			new->i_state = 0;
			inode = inode_insert5(new, hashval, test, set, data);
			if (unlikely(inode != new))
				destroy_inode(new);
		}
	}
	return inode;
}

struct inode *iget_locked(struct super_block *sb, unsigned long ino)
{
	struct hlist_head *head = inode_hashtable + hash(sb, ino);
	struct inode *inode;
again:
	spin_lock(&inode_hash_lock);
	inode = find_inode_fast(sb, head, ino);
	spin_unlock(&inode_hash_lock);
	if (inode) {
		if (IS_ERR(inode))
			return NULL;
		wait_on_inode(inode);
		if (unlikely(inode_unhashed(inode))) {
			iput(inode);
			goto again;
		}
		return inode;
	}

	inode = alloc_inode(sb);
	if (inode) {
		struct inode *old;

		spin_lock(&inode_hash_lock);
		
		old = find_inode_fast(sb, head, ino);
		if (!old) {
			inode->i_ino = ino;
			spin_lock(&inode->i_lock);
			inode->i_state = I_NEW;
			hlist_add_head_rcu(&inode->i_hash, head);
			spin_unlock(&inode->i_lock);
			inode_sb_list_add(inode);
			spin_unlock(&inode_hash_lock);

			return inode;
		}

		spin_unlock(&inode_hash_lock);
		destroy_inode(inode);
		if (IS_ERR(old))
			return NULL;
		inode = old;
		wait_on_inode(inode);
		if (unlikely(inode_unhashed(inode))) {
			iput(inode);
			goto again;
		}
	}
	return inode;
}

static int test_inode_iunique(struct super_block *sb, unsigned long ino)
{
	struct hlist_head *b = inode_hashtable + hash(sb, ino);
	struct inode *inode;

	hlist_for_each_entry_rcu(inode, b, i_hash) {
		if (inode->i_ino == ino && inode->i_sb == sb)
			return 0;
	}
	return 1;
}

/* Stub: not used in minimal kernel */
ino_t iunique(struct super_block *sb, ino_t max_reserved) { return 0; }

/* Stub: igrab not used in minimal kernel */
struct inode *igrab(struct inode *inode) { return inode ? __iget(inode), inode : NULL; }

struct inode *ilookup5_nowait(struct super_block *sb, unsigned long hashval,
		int (*test)(struct inode *, void *), void *data)
{
	struct hlist_head *head = inode_hashtable + hash(sb, hashval);
	struct inode *inode;

	spin_lock(&inode_hash_lock);
	inode = find_inode(sb, head, test, data);
	spin_unlock(&inode_hash_lock);

	return IS_ERR(inode) ? NULL : inode;
}

/* Stubbed - not used externally */
struct inode *ilookup5(struct super_block *sb, unsigned long hashval,
		int (*test)(struct inode *, void *), void *data)
{
	return NULL;
}

/* Stub: not used in minimal kernel */
struct inode *ilookup(struct super_block *sb, unsigned long ino) { return NULL; }

/* Stub: not used in minimal kernel */
struct inode *find_inode_nowait(struct super_block *sb, unsigned long hashval,
		int (*match)(struct inode *, unsigned long, void *), void *data) { return NULL; }

/* Stub: not used in minimal kernel */
struct inode *find_inode_rcu(struct super_block *sb, unsigned long hashval,
			     int (*test)(struct inode *, void *), void *data) { return NULL; }

/* Stub: not used in minimal kernel */
struct inode *find_inode_by_ino_rcu(struct super_block *sb,
				    unsigned long ino) { return NULL;
}

/* Stub: insert_inode_locked not used in minimal kernel */
int insert_inode_locked(struct inode *inode)
{
	return -EBUSY;
}

/* Stub: insert_inode_locked4 not used in minimal kernel */
int insert_inode_locked4(struct inode *inode, unsigned long hashval,
		int (*test)(struct inode *, void *), void *data)
{
	return -EBUSY;
}

/* Stub: generic_delete_inode not used externally */
int generic_delete_inode(struct inode *inode) { return 1; }

static void iput_final(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	const struct super_operations *op = inode->i_sb->s_op;
	unsigned long state;
	int drop;

	WARN_ON(inode->i_state & I_NEW);

	if (op->drop_inode)
		drop = op->drop_inode(inode);
	else
		drop = generic_drop_inode(inode);

	if (!drop &&
	    !(inode->i_state & I_DONTCACHE) &&
	    (sb->s_flags & SB_ACTIVE)) {
		__inode_add_lru(inode, true);
		spin_unlock(&inode->i_lock);
		return;
	}

	state = inode->i_state;
	if (!drop) {
		WRITE_ONCE(inode->i_state, state | I_WILL_FREE);
		spin_unlock(&inode->i_lock);

		write_inode_now(inode, 1);

		spin_lock(&inode->i_lock);
		state = inode->i_state;
		WARN_ON(state & I_NEW);
		state &= ~I_WILL_FREE;
	}

	WRITE_ONCE(inode->i_state, state | I_FREEING);
	if (!list_empty(&inode->i_lru))
		inode_lru_list_del(inode);
	spin_unlock(&inode->i_lock);

	evict(inode);
}

void iput(struct inode *inode)
{
	if (!inode)
		return;
	BUG_ON(inode->i_state & I_CLEAR);
retry:
	if (atomic_dec_and_lock(&inode->i_count, &inode->i_lock)) {
		if (inode->i_nlink && (inode->i_state & I_DIRTY_TIME)) {
			atomic_inc(&inode->i_count);
			spin_unlock(&inode->i_lock);
			trace_writeback_lazytime_iput(inode);
			mark_inode_dirty_sync(inode);
			goto retry;
		}
		iput_final(inode);
	}
}

static int relatime_need_update(struct vfsmount *mnt, struct inode *inode,
			     struct timespec64 now)
{

	if (!(mnt->mnt_flags & MNT_RELATIME))
		return 1;
	
	if (timespec64_compare(&inode->i_mtime, &inode->i_atime) >= 0)
		return 1;
	
	if (timespec64_compare(&inode->i_ctime, &inode->i_atime) >= 0)
		return 1;

	if ((long)(now.tv_sec - inode->i_atime.tv_sec) >= 24*60*60)
		return 1;
	
	return 0;
}

int generic_update_time(struct inode *inode, struct timespec64 *time, int flags)
{
	int dirty_flags = 0;

	if (flags & (S_ATIME | S_CTIME | S_MTIME)) {
		if (flags & S_ATIME)
			inode->i_atime = *time;
		if (flags & S_CTIME)
			inode->i_ctime = *time;
		if (flags & S_MTIME)
			inode->i_mtime = *time;

		if (inode->i_sb->s_flags & SB_LAZYTIME)
			dirty_flags |= I_DIRTY_TIME;
		else
			dirty_flags |= I_DIRTY_SYNC;
	}

	if ((flags & S_VERSION) && inode_maybe_inc_iversion(inode, false))
		dirty_flags |= I_DIRTY_SYNC;

	__mark_inode_dirty(inode, dirty_flags);
	return 0;
}

int inode_update_time(struct inode *inode, struct timespec64 *time, int flags)
{
	if (inode->i_op->update_time)
		return inode->i_op->update_time(inode, time, flags);
	return generic_update_time(inode, time, flags);
}

bool atime_needs_update(const struct path *path, struct inode *inode)
{
	struct vfsmount *mnt = path->mnt;
	struct timespec64 now;

	if (inode->i_flags & S_NOATIME)
		return false;

	if (HAS_UNMAPPED_ID(mnt_user_ns(mnt), inode))
		return false;

	if (IS_NOATIME(inode))
		return false;
	if ((inode->i_sb->s_flags & SB_NODIRATIME) && S_ISDIR(inode->i_mode))
		return false;

	if (mnt->mnt_flags & MNT_NOATIME)
		return false;
	if ((mnt->mnt_flags & MNT_NODIRATIME) && S_ISDIR(inode->i_mode))
		return false;

	now = current_time(inode);

	if (!relatime_need_update(mnt, inode, now))
		return false;

	if (timespec64_equal(&inode->i_atime, &now))
		return false;

	return true;
}

void touch_atime(const struct path *path)
{
	struct vfsmount *mnt = path->mnt;
	struct inode *inode = d_inode(path->dentry);
	struct timespec64 now;

	if (!atime_needs_update(path, inode))
		return;

	if (!sb_start_write_trylock(inode->i_sb))
		return;

	if (__mnt_want_write(mnt) != 0)
		goto skip_update;
	
	now = current_time(inode);
	inode_update_time(inode, &now, S_ATIME);
	__mnt_drop_write(mnt);
skip_update:
	sb_end_write(inode->i_sb);
}

int should_remove_suid(struct dentry *dentry)
{
	umode_t mode = d_inode(dentry)->i_mode;
	int kill = 0;

	if (unlikely(mode & S_ISUID))
		kill = ATTR_KILL_SUID;

	if (unlikely((mode & S_ISGID) && (mode & S_IXGRP)))
		kill |= ATTR_KILL_SGID;

	if (unlikely(kill && !capable(CAP_FSETID) && S_ISREG(mode)))
		return kill;

	return 0;
}

int dentry_needs_remove_privs(struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	int mask = 0;
	int ret;

	if (IS_NOSEC(inode))
		return 0;

	mask = should_remove_suid(dentry);
	ret = security_inode_need_killpriv(dentry);
	if (ret < 0)
		return ret;
	if (ret)
		mask |= ATTR_KILL_PRIV;
	return mask;
}

static int __remove_privs(struct user_namespace *mnt_userns,
			  struct dentry *dentry, int kill)
{
	struct iattr newattrs;

	newattrs.ia_valid = ATTR_FORCE | kill;
	
	return notify_change(mnt_userns, dentry, &newattrs, NULL);
}

int file_remove_privs(struct file *file)
{
	struct dentry *dentry = file_dentry(file);
	struct inode *inode = file_inode(file);
	int kill;
	int error = 0;

	if (IS_NOSEC(inode) || !S_ISREG(inode->i_mode))
		return 0;

	kill = dentry_needs_remove_privs(dentry);
	if (kill < 0)
		return kill;
	if (kill)
		error = __remove_privs(file_mnt_user_ns(file), dentry, kill);
	if (!error)
		inode_has_no_xattr(inode);

	return error;
}

int file_update_time(struct file *file)
{
	struct inode *inode = file_inode(file);
	struct timespec64 now;
	int sync_it = 0;
	int ret;

	if (IS_NOCMTIME(inode))
		return 0;

	now = current_time(inode);
	if (!timespec64_equal(&inode->i_mtime, &now))
		sync_it = S_MTIME;

	if (!timespec64_equal(&inode->i_ctime, &now))
		sync_it |= S_CTIME;

	if (IS_I_VERSION(inode) && inode_iversion_need_inc(inode))
		sync_it |= S_VERSION;

	if (!sync_it)
		return 0;

	if (__mnt_want_write_file(file))
		return 0;

	ret = inode_update_time(inode, &now, sync_it);
	__mnt_drop_write_file(file);

	return ret;
}

int file_modified(struct file *file)
{
	int err;

	err = file_remove_privs(file);
	if (err)
		return err;

	if (unlikely(file->f_mode & FMODE_NOCMTIME))
		return 0;

	return file_update_time(file);
}

/* Stub: inode_needs_sync not used externally */
int inode_needs_sync(struct inode *inode) { return 0; }

static void __wait_on_freeing_inode(struct inode *inode)
{
	wait_queue_head_t *wq;
	DEFINE_WAIT_BIT(wait, &inode->i_state, __I_NEW);
	wq = bit_waitqueue(&inode->i_state, __I_NEW);
	prepare_to_wait(wq, &wait.wq_entry, TASK_UNINTERRUPTIBLE);
	spin_unlock(&inode->i_lock);
	spin_unlock(&inode_hash_lock);
	schedule();
	finish_wait(wq, &wait.wq_entry);
	spin_lock(&inode_hash_lock);
}

static __initdata unsigned long ihash_entries;
static int __init set_ihash_entries(char *str)
{
	if (!str)
		return 0;
	ihash_entries = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("ihash_entries=", set_ihash_entries);

void __init inode_init_early(void)
{
	
	if (hashdist)
		return;

	inode_hashtable =
		alloc_large_system_hash("Inode-cache",
					sizeof(struct hlist_head),
					ihash_entries,
					14,
					HASH_EARLY | HASH_ZERO,
					&i_hash_shift,
					&i_hash_mask,
					0,
					0);
}

void __init inode_init(void)
{
	
	inode_cachep = kmem_cache_create("inode_cache",
					 sizeof(struct inode),
					 0,
					 (SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
					 SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					 init_once);

	if (!hashdist)
		return;

	inode_hashtable =
		alloc_large_system_hash("Inode-cache",
					sizeof(struct hlist_head),
					ihash_entries,
					14,
					HASH_ZERO,
					&i_hash_shift,
					&i_hash_mask,
					0,
					0);
}

void init_special_inode(struct inode *inode, umode_t mode, dev_t rdev)
{
	inode->i_mode = mode;
	if (S_ISCHR(mode)) {
		inode->i_fop = &def_chr_fops;
		inode->i_rdev = rdev;
	} else if (S_ISBLK(mode)) {
		inode->i_fop = &def_blk_fops;
		inode->i_rdev = rdev;
	} else if (S_ISFIFO(mode))
		inode->i_fop = &pipefifo_fops;
	else if (S_ISSOCK(mode))
		;	
	else
		printk(KERN_DEBUG "init_special_inode: bogus i_mode (%o) for"
				  " inode %s:%lu\n", mode, inode->i_sb->s_id,
				  inode->i_ino);
}

void inode_init_owner(struct user_namespace *mnt_userns, struct inode *inode,
		      const struct inode *dir, umode_t mode)
{
	inode_fsuid_set(inode, mnt_userns);
	if (dir && dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;

		if (S_ISDIR(mode))
			mode |= S_ISGID;
		else if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP) &&
			 !in_group_p(i_gid_into_mnt(mnt_userns, dir)) &&
			 !capable_wrt_inode_uidgid(mnt_userns, dir, CAP_FSETID))
			mode &= ~S_ISGID;
	} else
		inode_fsgid_set(inode, mnt_userns);
	inode->i_mode = mode;
}

bool inode_owner_or_capable(struct user_namespace *mnt_userns,
			    const struct inode *inode)
{
	kuid_t i_uid;
	struct user_namespace *ns;

	i_uid = i_uid_into_mnt(mnt_userns, inode);
	if (uid_eq(current_fsuid(), i_uid))
		return true;

	ns = current_user_ns();
	if (kuid_has_mapping(ns, i_uid) && ns_capable(ns, CAP_FOWNER))
		return true;
	return false;
}

/* Stub: inode_dio_wait not used externally in minimal kernel */
void inode_dio_wait(struct inode *inode)
{
}

/* Stub: inode_set_flags not used in minimal kernel */
void inode_set_flags(struct inode *inode, unsigned int flags,
		     unsigned int mask)
{
}

void inode_nohighmem(struct inode *inode)
{
	mapping_set_gfp_mask(inode->i_mapping, GFP_USER);
}

struct timespec64 timestamp_truncate(struct timespec64 t, struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	unsigned int gran = sb->s_time_gran;

	t.tv_sec = clamp(t.tv_sec, sb->s_time_min, sb->s_time_max);
	if (unlikely(t.tv_sec == sb->s_time_max || t.tv_sec == sb->s_time_min))
		t.tv_nsec = 0;

	if (gran == 1)
		; 
	else if (gran == NSEC_PER_SEC)
		t.tv_nsec = 0;
	else if (gran > 1 && gran < NSEC_PER_SEC)
		t.tv_nsec -= t.tv_nsec % gran;
	else
		WARN(1, "invalid file time granularity: %u", gran);
	return t;
}

struct timespec64 current_time(struct inode *inode)
{
	struct timespec64 now;

	ktime_get_coarse_real_ts64(&now);

	if (unlikely(!inode->i_sb)) {
		WARN(1, "current_time() called with uninitialized super_block in the inode");
		return now;
	}

	return timestamp_truncate(now, inode);
}
