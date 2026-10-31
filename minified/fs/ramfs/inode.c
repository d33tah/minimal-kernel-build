

#include <linux/pagemap.h>
#include <linux/ramfs.h>
#include "internal.h"

#define RAMFS_DEFAULT_MODE 0755

static const struct inode_operations ramfs_dir_inode_operations;

static struct inode *ramfs_get_inode(struct super_block *sb,
				     const struct inode *dir, umode_t mode,
				     dev_t dev)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(&init_user_ns, inode, dir, mode);
		inode->i_mapping->a_ops = &ram_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &ramfs_file_inode_operations;
			inode->i_fop = &ramfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &ramfs_dir_inode_operations;

			inc_nlink(inode);
		}
	}
	return inode;
}

static int ramfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
		       struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode = ramfs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
	}
	return error;
}

static int ramfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
		       struct dentry *dentry, umode_t mode)
{
	int retval = ramfs_mknod(&init_user_ns, dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int ramfs_create(struct user_namespace *mnt_userns, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	return ramfs_mknod(&init_user_ns, dir, dentry, mode | S_IFREG, 0);
}

static const struct inode_operations ramfs_dir_inode_operations = {
	.create = ramfs_create,
	.lookup = simple_lookup,
	.mkdir = ramfs_mkdir,
	.mknod = ramfs_mknod,
};

static int ramfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct inode *inode;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;

	inode = ramfs_get_inode(sb, NULL, S_IFDIR | RAMFS_DEFAULT_MODE, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	return 0;
}

static int ramfs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, ramfs_fill_super);
}

static const struct fs_context_operations ramfs_context_ops = {
	.get_tree = ramfs_get_tree,
};

int ramfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &ramfs_context_ops;
	return 0;
}
