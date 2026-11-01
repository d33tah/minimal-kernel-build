// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 1997 Linus Torvalds
 * (C) 1999 Andrea Arcangeli <andrea@suse.de> (dynamic inode allocation)
 */
#include <linux/swap.h>

static const struct address_space_operations empty_aops = {};

static int no_open(struct inode *inode, struct file *file)
{
	return -ENXIO;
}

static int inode_init_always(struct super_block *sb, struct inode *inode)
{
	static const struct inode_operations empty_iops;
	static const struct file_operations no_open_fops = { .open = no_open };
	struct address_space *const mapping = &inode->i_data;

	inode->i_sb = sb;
	inode->i_op = &empty_iops;
	inode->i_fop = &no_open_fops;
	i_uid_write(inode, 0);
	i_gid_write(inode, 0);
	atomic_set(&inode->i_writecount, 0);
	inode->i_size = 0;
	inode->i_cdev = NULL;
	inode->i_rdev = 0;

	spin_lock_init(&inode->i_lock);
	init_rwsem(&inode->i_rwsem);

	mapping->a_ops = &empty_aops;
	mapping->host = inode;
	mapping_set_gfp_mask(mapping, GFP_HIGHUSER_MOVABLE);
	init_rwsem(&mapping->invalidate_lock);
	inode->i_mapping = mapping;

	return 0;
}

static void __address_space_init_once(struct address_space *mapping)
{
	xa_init_flags(&mapping->i_pages, XA_FLAGS_LOCK_IRQ | XA_FLAGS_ACCOUNT);
}

static void init_once(void *foo)
{
	struct inode *inode = (struct inode *)foo;

	memset(inode, 0, sizeof(*inode));
	__address_space_init_once(&inode->i_data);
}

static struct inode *alloc_inode(struct super_block *sb)
{
	struct inode *inode;

	inode = kmalloc(sizeof(*inode), GFP_KERNEL);

	if (!inode)
		return NULL;
	init_once(inode);

	inode_init_always(sb, inode);

	return inode;
}

static struct inode *new_inode_pseudo(struct super_block *sb)
{
	struct inode *inode = alloc_inode(sb);

	return inode;
}

/**
 *	new_inode 	- obtain an inode
 *	@sb: superblock
 *
 *	Allocates a new inode for given superblock. The default gfp_mask
 *	for allocations related to inode->i_mapping is GFP_HIGHUSER_MOVABLE.
 *	If HIGHMEM pages are unsuitable or it is known that pages allocated
 *	for the page cache are not reclaimable or migratable,
 *	mapping_set_gfp_mask() must be called with suitable flags on the
 *	newly created inode's mapping
 *
 */
struct inode *new_inode(struct super_block *sb)
{
	struct inode *inode;

	inode = new_inode_pseudo(sb);
	return inode;
}

void init_special_inode(struct inode *inode, umode_t mode, dev_t rdev)
{
	inode->i_mode = mode;
	if (S_ISCHR(mode)) {
		inode->i_fop = &def_chr_fops;
		inode->i_rdev = rdev;
	}
}

/**
 * inode_init_owner - Init uid,gid,mode for new inode according to posix standards
 * @mnt_userns:	User namespace of the mount the inode was created from
 * @inode: New inode
 * @dir: Directory inode
 * @mode: mode of the new inode
 *
 * If the inode has been created through an idmapped mount the user namespace of
 * the vfsmount must be passed through @mnt_userns. This function will then take
 * care to map the inode according to @mnt_userns before checking permissions
 * and initializing i_uid and i_gid. On non-idmapped mounts or if permission
 * checking is to be performed on the raw inode simply passs init_user_ns.
 */
void inode_init_owner(struct user_namespace *mnt_userns, struct inode *inode,
		      const struct inode *dir, umode_t mode)
{
	inode_fsuid_set(inode, mnt_userns);
	if (dir && dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;

		/* Directories are special, and always inherit S_ISGID */
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode_fsgid_set(inode, mnt_userns);
	inode->i_mode = mode;
}
