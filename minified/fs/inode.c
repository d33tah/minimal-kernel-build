
#include <linux/fs.h>
#include <linux/mm.h>
/* backing-dev.h removed - unused */
#include <linux/hash.h>
#include <linux/swap.h>
/* fsnotify.h, tracepoint.h, ratelimit.h removed - unused */
#include <linux/security.h>
#include <linux/cdev.h>
#include <linux/memblock.h>
#include <linux/mount.h>

/* inode_has_buffers removed - always returned 0 */
#include <linux/list_lru.h>
#include "internal.h"

static unsigned int i_hash_mask __read_mostly;
static unsigned int i_hash_shift __read_mostly;
static struct hlist_head *inode_hashtable __read_mostly;
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(inode_hash_lock);

const struct address_space_operations empty_aops = {};

/* pipefifo_fops moved here from fs/pipe.c */
const struct file_operations pipefifo_fops = {};

/* Merged from no-block.c (CONFIG_BLOCK not set) */
static int no_blkdev_open(struct inode *inode, struct file *filp)
{
	return -ENODEV;
}
static const struct file_operations def_blk_fops = { .open = no_blkdev_open };

/* nr_inodes, nr_unused removed - only inc/dec, never read */

static struct kmem_cache *inode_cachep __read_mostly;

static int no_open(struct inode *inode, struct file *file)
{
	return -ENXIO;
}

/* Changed to void - always returned 0, no callers check return value */
void inode_init_always(struct super_block *sb, struct inode *inode)
{
	static const struct inode_operations empty_iops;
	static const struct file_operations no_open_fops = { .open = no_open };
	struct address_space *const mapping = &inode->i_data;

	inode->i_sb = sb;
	inode->i_blkbits = sb->s_blocksize_bits;
	inode->i_flags = 0;
	atomic_set(&inode->i_count, 1);
	inode->i_op = &empty_iops;
	inode->i_fop = &no_open_fops;
	inode->i_ino = 0;
	inode->__i_nlink = 1;
	inode->i_opflags = 0;
	if (sb->s_xattr)
		inode->i_opflags |= IOP_XATTR;
	/* i_uid_write, i_gid_write inlined */
	inode->i_uid = make_kuid(i_user_ns(inode), 0);
	inode->i_gid = make_kgid(i_user_ns(inode), 0);
	atomic_set(&inode->i_writecount, 0);
	inode->i_size = 0;
	inode->i_cdev = NULL;
	/* i_link init removed - field removed from union */
	inode->i_dir_seq = 0;
	inode->i_rdev = 0;

	/* security_inode_alloc always returns 0 */
	spin_lock_init(&inode->i_lock);
	/* lockdep_set_class removed - empty stub */

	init_rwsem(&inode->i_rwsem);
	/* lockdep_set_class removed - empty stub */

	mapping->a_ops = &empty_aops;
	mapping->host = inode;
	mapping->flags = 0;
	/* mapping->wb_err initialization removed - field removed */
	atomic_set(&mapping->i_mmap_writable, 0);
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	/* mapping->private_data removed - field removed */
	init_rwsem(&mapping->invalidate_lock);
	/* lockdep_set_class_and_name removed - empty stub */
	/* inode->i_private removed - unused */
	inode->i_mapping = mapping;
	INIT_HLIST_HEAD(&inode->i_dentry);
}

static void i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	/* free_inode callback removed - never assigned */
	kmem_cache_free(inode_cachep, inode);
}

static struct inode *alloc_inode(struct super_block *sb)
{
	struct inode *inode;

	/* alloc_inode callback removed - never assigned */
	inode = alloc_inode_sb(sb, inode_cachep, GFP_KERNEL);

	if (!inode)
		return NULL;

	inode_init_always(sb, inode);

	return inode;
}

static void __destroy_inode(struct inode *inode)
{
	/* BUG_ON(inode_has_buffers) removed - always 0 */
	if (!inode->i_nlink) {
		WARN_ON(atomic_long_read(&inode->i_sb->s_remove_count) == 0);
		atomic_long_dec(&inode->i_sb->s_remove_count);
	}
	/* nr_inodes counter removed */
}

/* drop_nlink removed - never called */

void clear_nlink(struct inode *inode)
{
	if (inode->i_nlink) {
		inode->__i_nlink = 0;
		atomic_long_inc(&inode->i_sb->s_remove_count);
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

void inode_init_once(struct inode *inode)
{
	struct address_space *mapping = &inode->i_data;
	memset(inode, 0, sizeof(*inode));
	INIT_HLIST_NODE(&inode->i_hash);
	INIT_LIST_HEAD(&inode->i_devices);
	INIT_LIST_HEAD(&inode->i_lru);
	/* Inlined __address_space_init_once */
	xa_init_flags(&mapping->i_pages, XA_FLAGS_LOCK_IRQ | XA_FLAGS_ACCOUNT);
	init_rwsem(&mapping->i_mmap_rwsem);
	INIT_LIST_HEAD(&mapping->private_list);
	/* spin_lock_init(&mapping->private_lock) removed - field removed */
	mapping->i_mmap = RB_ROOT_CACHED;
}

static void init_once(void *foo)
{
	struct inode *inode = (struct inode *)foo;

	inode_init_once(inode);
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

	if (!list_lru_add(&inode->i_sb->s_inode_lru, &inode->i_lru) && rotate)
		inode->i_state |= I_REFERENCED;
	/* nr_unused counter removed */
}

void inode_add_lru(struct inode *inode)
{
	__inode_add_lru(inode, false);
}

static void inode_lru_list_del(struct inode *inode)
{
	list_lru_del(&inode->i_sb->s_inode_lru, &inode->i_lru);
	/* nr_unused counter removed */
}

void inode_sb_list_add(struct inode *inode)
{
	spin_lock(&inode->i_sb->s_inode_list_lock);
	list_add(&inode->i_sb_list, &inode->i_sb->s_inodes);
	spin_unlock(&inode->i_sb->s_inode_list_lock);
}

/* inode_sb_list_del inlined into evict() */

void __remove_inode_hash(struct inode *inode)
{
	spin_lock(&inode_hash_lock);
	spin_lock(&inode->i_lock);
	hlist_del_init_rcu(&inode->i_hash);
	spin_unlock(&inode->i_lock);
	spin_unlock(&inode_hash_lock);
}

void clear_inode(struct inode *inode)
{
	xa_lock_irq(&inode->i_data.i_pages);
	BUG_ON(inode->i_data.nrpages);

	xa_unlock_irq(&inode->i_data.i_pages);
	BUG_ON(!list_empty(&inode->i_data.private_list));
	BUG_ON(!(inode->i_state & I_FREEING));
	BUG_ON(inode->i_state & I_CLEAR);
	/* i_wb_list BUG_ON removed - field unused */

	inode->i_state = I_FREEING | I_CLEAR;
}

static void evict(struct inode *inode)
{
	BUG_ON(!(inode->i_state & I_FREEING));
	BUG_ON(!list_empty(&inode->i_lru));

	/* inode_io_list_del, inode_wait_for_writeback removed - empty stubs */

	/* Inlined inode_sb_list_del */
	if (!list_empty(&inode->i_sb_list)) {
		spin_lock(&inode->i_sb->s_inode_list_lock);
		list_del_init(&inode->i_sb_list);
		spin_unlock(&inode->i_sb->s_inode_list_lock);
	}

	/* evict_inode callback removed - never assigned */
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);

	if (S_ISCHR(inode->i_mode) && inode->i_cdev)
		cd_forget(inode);

	/* remove_inode_hash inlined, hlist_fake inlined */
	if (!inode_unhashed(inode) &&
	    !(inode->i_hash.pprev == &inode->i_hash.next))
		__remove_inode_hash(inode);

	spin_lock(&inode->i_lock);
	wake_up_bit(&inode->i_state, __I_NEW);
	BUG_ON(inode->i_state != (I_FREEING | I_CLEAR));
	spin_unlock(&inode->i_lock);

	/* destroy_inode and free_inode callbacks removed - never assigned */
	BUG_ON(!list_empty(&inode->i_lru));
	__destroy_inode(inode);
	/* free_inode is NULL since callback was never assigned */
	call_rcu(&inode->i_rcu, i_callback);
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

/* Inode cache shrinking stubbed - not needed for minimal boot */
long prune_icache_sb(struct super_block *sb, struct shrink_control *sc)
{
	return 0;
}

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

static struct inode *new_inode_pseudo(struct super_block *sb)
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

/* Used by ramfs */
int generic_delete_inode(struct inode *inode)
{
	return 1;
}

void iput(struct inode *inode)
{
	struct super_block *sb;
	const struct super_operations *op;
	unsigned long state;
	int drop;

	if (!inode)
		return;
	BUG_ON(inode->i_state & I_CLEAR);
	if (!atomic_dec_and_lock(&inode->i_count, &inode->i_lock))
		return;

	sb = inode->i_sb;
	op = inode->i_sb->s_op;

	WARN_ON(inode->i_state & I_NEW);

	if (op->drop_inode)
		drop = op->drop_inode(inode);
	else
		/* generic_drop_inode inlined */
		drop = !inode->i_nlink || inode_unhashed(inode);

	if (!drop && !(inode->i_state & I_DONTCACHE) &&
	    (sb->s_flags & SB_ACTIVE)) {
		__inode_add_lru(inode, true);
		spin_unlock(&inode->i_lock);
		return;
	}

	state = inode->i_state;
	if (!drop) {
		WRITE_ONCE(inode->i_state, state | I_WILL_FREE);
		spin_unlock(&inode->i_lock);

		/* write_inode_now removed - returns 0 stub */

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

/* inode_update_time, atime_needs_update, touch_atime removed - never called / empty stubs */
/* dentry_needs_remove_privs, file_remove_privs removed - no callers */
/* file_update_time removed - inlined into fs.h as 0 return stub */

static __initdata unsigned long ihash_entries;

void __init inode_init_early(void)
{
	if (hashdist)
		return;

	inode_hashtable = alloc_large_system_hash(
		"Inode-cache", sizeof(struct hlist_head), ihash_entries, 14,
		HASH_EARLY | HASH_ZERO, &i_hash_shift, &i_hash_mask, 0, 0);
}

void __init inode_init(void)
{
	inode_cachep = kmem_cache_create("inode_cache", sizeof(struct inode), 0,
					 (SLAB_RECLAIM_ACCOUNT | SLAB_PANIC |
					  SLAB_MEM_SPREAD | SLAB_ACCOUNT),
					 init_once);

	if (!hashdist)
		return;

	inode_hashtable = alloc_large_system_hash(
		"Inode-cache", sizeof(struct hlist_head), ihash_entries, 14,
		HASH_ZERO, &i_hash_shift, &i_hash_mask, 0, 0);
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
				  " inode %s:%lu\n",
		       mode, inode->i_sb->s_id, inode->i_ino);
}

void inode_init_owner(struct user_namespace *mnt_userns, struct inode *inode,
		      const struct inode *dir, umode_t mode)
{
	/* inode_fsuid_set inlined, mapped_fsuid inlined */
	inode->i_uid =
		mapped_kuid_fs(mnt_userns, i_user_ns(inode), current_fsuid());
	if (dir && dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;

		if (S_ISDIR(mode))
			mode |= S_ISGID;
		else if ((mode & (S_ISGID | S_IXGRP)) == (S_ISGID | S_IXGRP) &&
			 !in_group_p(i_gid_into_mnt(mnt_userns, dir)) &&
			 !capable_wrt_inode_uidgid(mnt_userns, dir, CAP_FSETID))
			mode &= ~S_ISGID;
	} else
		/* inode_fsgid_set inlined, mapped_fsgid inlined */
		inode->i_gid = mapped_kgid_fs(mnt_userns, i_user_ns(inode),
					      current_fsgid());
	inode->i_mode = mode;
}

bool inode_owner_or_capable(struct user_namespace *mnt_userns,
			    const struct inode *inode)
{
	kuid_t i_uid;

	i_uid = i_uid_into_mnt(mnt_userns, inode);
	if (uid_eq(current_fsuid(), i_uid))
		return true;

	/* ns_capable always returns true - simplified check */
	return kuid_has_mapping(current_user_ns(), i_uid);
}

/* inode_nohighmem, timestamp_truncate, current_time removed - no timestamp fields in inode / never called */
