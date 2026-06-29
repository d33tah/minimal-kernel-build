
#include <linux/fs.h>
#include <linux/swap.h>
#include <linux/mount.h>

#include "internal.h"

/* iversion machinery removed: SB_I_VERSION is never set on any superblock in
 * this build (ramfs/devtmpfs/proc/sysfs), so IS_I_VERSION() is always false and
 * the S_VERSION sync_it branch never fires. i_version was never written. */

/*
 * Inode hashing removed: this build has no __insert_inode_hash, so every inode's
 * i_hash stays INIT_HLIST_NODE (inode_unhashed() always true). The hash table was
 * write-only (inode_hashtable never read), so the alloc + __remove_inode_hash were
 * pure dead work.
 */
const struct address_space_operations empty_aops = {
};

static struct kmem_cache *inode_cachep __read_mostly;



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
	atomic_set(&inode->i_count, 1);
	inode->i_op = &empty_iops;
	inode->i_fop = &no_open_fops;
	inode->i_ino = 0;
	inode->__i_nlink = 1;
	inode->i_opflags = 0;
	i_uid_write(inode, 0);
	i_gid_write(inode, 0);
	atomic_set(&inode->i_writecount, 0);
	inode->i_size = 0;
	inode->i_cdev = NULL;
	inode->i_dir_seq = 0;
	inode->i_rdev = 0;

	spin_lock_init(&inode->i_lock);
	init_rwsem(&inode->i_rwsem);

	mapping->a_ops = &empty_aops;
	mapping->host = inode;
	mapping->flags = 0;
	atomic_set(&mapping->i_mmap_writable, 0);
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	init_rwsem(&mapping->invalidate_lock);
	inode->i_mapping = mapping;
	INIT_HLIST_HEAD(&inode->i_dentry);

	return 0;
}

static void free_inode_nonrcu(struct inode *inode)
{
	kmem_cache_free(inode_cachep, inode);
}

static void i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	free_inode_nonrcu(inode);
}

static struct inode *alloc_inode(struct super_block *sb)
{
	struct inode *inode;

	inode = alloc_inode_sb(sb, inode_cachep, GFP_KERNEL);

	if (!inode)
		return NULL;

	if (unlikely(inode_init_always(sb, inode))) {
		i_callback(&inode->i_rcu);
		return NULL;
	}

	return inode;
}

void inc_nlink(struct inode *inode)
{
	inode->__i_nlink++;
}

static void __address_space_init_once(struct address_space *mapping)
{
	xa_init_flags(&mapping->i_pages, XA_FLAGS_LOCK_IRQ | XA_FLAGS_ACCOUNT);
	init_rwsem(&mapping->i_mmap_rwsem);
	mapping->i_mmap = RB_ROOT_CACHED;
}


static void init_once(void *foo)
{
	struct inode *inode = (struct inode *) foo;

	memset(inode, 0, sizeof(*inode));
	INIT_HLIST_NODE(&inode->i_hash);
	INIT_LIST_HEAD(&inode->i_devices);
	INIT_LIST_HEAD(&inode->i_lru);
	__address_space_init_once(&inode->i_data);
	i_size_ordered_init(inode);
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

struct inode *new_inode_pseudo(struct super_block *sb)
{
	struct inode *inode = alloc_inode(sb);

	if (inode) {
		spin_lock(&inode->i_lock);
		inode->i_state = 0;
		spin_unlock(&inode->i_lock);
	}
	return inode;
}

struct inode *new_inode(struct super_block *sb)
{
	struct inode *inode;

	inode = new_inode_pseudo(sb);
	return inode;
}

/* Used by ramfs */
int generic_delete_inode(struct inode *inode) { return 1; }

/*
 * Runtime-dead on this single-shot boot: nothing is ever the last reference to
 * an inode (init/idle never closes its files), so the whole inode-teardown
 * subtree (iput_final, evict, destroy_inode, __inode_add_lru, inode_lru_list_del,
 * truncate_inode_pages_final, cd_forget) never executes.  Stubbed to a no-op:
 * an inode whose count never drops to zero simply stays referenced -- harmless
 * on a system that never destroys inodes.
 */
void iput(struct inode *inode)
{
}

static int generic_update_time(struct inode *inode, struct timespec64 *time, int flags)
{
	if (flags & (S_ATIME | S_CTIME | S_MTIME)) {
		if (flags & S_ATIME)
			inode->i_atime = *time;
		if (flags & S_CTIME)
			inode->i_ctime = *time;
		if (flags & S_MTIME)
			inode->i_mtime = *time;
	}

	return 0;
}

int inode_update_time(struct inode *inode, struct timespec64 *time, int flags)
{
	/* No live inode_operations sets ->update_time; always generic. */
	return generic_update_time(inode, time, flags);
}

bool atime_needs_update(const struct path *path, struct inode *inode)
{
	struct vfsmount *mnt = path->mnt;
	struct timespec64 now;

	if (HAS_UNMAPPED_ID(mnt_user_ns(mnt), inode))
		return false;

	if (IS_NOATIME(inode))
		return false;

	now = current_time(inode);

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

	if (__mnt_want_write(mnt) != 0)
		goto skip_update;
	
	now = current_time(inode);
	inode_update_time(inode, &now, S_ATIME);
	__mnt_drop_write(mnt);
skip_update:
	sb_end_write(inode->i_sb);
}

int file_remove_privs(struct file *file)
{
	/*
	 * No live path sets suid/sgid removal flags (should_remove_suid was a
	 * constant-0 stub), so there is never anything to strip here.
	 */
	return 0;
}

int file_update_time(struct file *file)
{
	struct inode *inode = file_inode(file);
	struct timespec64 now;
	int sync_it = 0;
	int ret;

	now = current_time(inode);
	if (!timespec64_equal(&inode->i_mtime, &now))
		sync_it = S_MTIME;

	if (!timespec64_equal(&inode->i_ctime, &now))
		sync_it |= S_CTIME;

	if (!sync_it)
		return 0;

	if (__mnt_want_write_file(file))
		return 0;

	ret = inode_update_time(inode, &now, sync_it);
	__mnt_drop_write_file(file);

	return ret;
}


void __init inode_init(void)
{
	
	inode_cachep = kmem_cache_create("inode_cache",
					 sizeof(struct inode),
					 0,
					 (SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
					 SLAB_ACCOUNT),
					 init_once);
}

void init_special_inode(struct inode *inode, umode_t mode, dev_t rdev)
{
	/* This minimal kernel's initramfs creates exactly one special node,
	 * /dev/console (char 5:1), so only the S_ISCHR branch ever fires at
	 * runtime; the block/fifo/sock branches are runtime-dead -> dropped
	 * (def_blk_fops/no_blkdev_open/pipefifo_fops cascade-deleted). */
	inode->i_mode = mode;
	if (S_ISCHR(mode)) {
		inode->i_fop = &def_chr_fops;
		inode->i_rdev = rdev;
	} else
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
	if (kuid_has_mapping(ns, i_uid))
		return true;
	return false;
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
