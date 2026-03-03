
#include <linux/swap.h>

static int _atomic_dec_and_lock(atomic_t *atomic, spinlock_t *lock)
{
	if (atomic_add_unless(atomic, -1, 1))
		return 0;
	spin_lock(lock);
	if (atomic_dec_and_test(atomic))
		return 1;
	spin_unlock(lock);
	return 0;
}
const struct address_space_operations empty_aops = {};

static struct kmem_cache *inode_cachep __read_mostly;

static int no_open(struct inode *inode, struct file *file)
{
	return -ENXIO;
}

static void inode_init_always(struct super_block *sb, struct inode *inode)
{
	static const struct inode_operations empty_iops;
	static const struct file_operations no_open_fops = { .open = no_open };
	struct address_space *const mapping = &inode->i_data;

	inode->i_sb = sb;
	inode->i_flags = 0;
	atomic_set(&inode->i_count, 1);
	inode->i_op = &empty_iops;
	inode->i_fop = &no_open_fops;
	inode->__i_nlink = 1;
	atomic_set(&inode->i_writecount, 0);
	inode->i_size = 0;
	inode->i_dir_seq = 0;

	spin_lock_init(&inode->i_lock);

	init_rwsem(&inode->i_rwsem);

	mapping->a_ops = &empty_aops;
	mapping->host = inode;
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	init_rwsem(&mapping->invalidate_lock);
	inode->i_mapping = mapping;
	INIT_HLIST_HEAD(&inode->i_dentry);
}

static void i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(inode_cachep, inode);
}

void inc_nlink(struct inode *inode)
{
	inode->__i_nlink++;
}

static void inode_init_once(struct inode *inode)
{
	struct address_space *mapping = &inode->i_data;
	memset(inode, 0, sizeof(*inode));
	INIT_LIST_HEAD(&inode->i_lru);
	xa_init_flags(&mapping->i_pages, XA_FLAGS_LOCK_IRQ | XA_FLAGS_ACCOUNT);
}

static void init_once(void *foo)
{
	struct inode *inode = (struct inode *)foo;

	inode_init_once(inode);
}

static void inode_lru_list_del(struct inode *inode)
{
	list_lru_del(&inode->i_sb->s_inode_lru, &inode->i_lru);
}

static void evict(struct inode *inode)
{
	if (!list_empty(&inode->i_sb_list)) {
		spin_lock(&inode->i_sb->s_inode_list_lock);
		list_del_init(&inode->i_sb_list);
		spin_unlock(&inode->i_sb->s_inode_list_lock);
	}

	inode->i_state = I_FREEING | I_CLEAR;

	spin_lock(&inode->i_lock);
	wake_up_bit(&inode->i_state, __I_NEW);
	spin_unlock(&inode->i_lock);

	call_rcu(&inode->i_rcu, i_callback);
}

void evict_inodes(struct super_block *sb)
{
	/* Hello World kernel never properly shuts down - stub */
}

static struct inode *new_inode_pseudo(struct super_block *sb)
{
	struct inode *inode;

	inode = alloc_inode_sb(sb, inode_cachep, GFP_KERNEL);
	if (!inode)
		return NULL;
	inode_init_always(sb, inode);

	spin_lock(&inode->i_lock);
	inode->i_state = 0;
	spin_unlock(&inode->i_lock);
	INIT_LIST_HEAD(&inode->i_sb_list);
	return inode;
}

struct inode *new_inode(struct super_block *sb)
{
	struct inode *inode;

	spin_lock_prefetch(&sb->s_inode_list_lock);

	inode = new_inode_pseudo(sb);
	if (inode) {
		spin_lock(&inode->i_sb->s_inode_list_lock);
		list_add(&inode->i_sb_list, &inode->i_sb->s_inodes);
		spin_unlock(&inode->i_sb->s_inode_list_lock);
	}
	return inode;
}

void iput(struct inode *inode)
{
	if (!inode)
		return;
	if (!_atomic_dec_and_lock(&inode->i_count, &inode->i_lock))
		return;

	WRITE_ONCE(inode->i_state, inode->i_state | I_FREEING);
	if (!list_empty(&inode->i_lru))
		inode_lru_list_del(inode);
	spin_unlock(&inode->i_lock);

	evict(inode);
}

void __init inode_init(void)
{
	inode_cachep = kmem_cache_create("inode_cache", sizeof(struct inode), 0,
					 (SLAB_RECLAIM_ACCOUNT | SLAB_PANIC |
					  SLAB_MEM_SPREAD | SLAB_ACCOUNT),
					 init_once);
	/* hashdist==0, so hash table allocated in inode_init_early */
}
