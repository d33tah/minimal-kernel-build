
#include <linux/blkdev.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/mount.h>
#include <linux/types.h>
#include <asm/statfs.h>
#include <asm/byteorder.h>

#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/writeback.h>
#include <linux/fs_context.h>

#include <linux/uaccess.h>

#include "internal.h"

/* simple_getattr, simple_statfs removed - callbacks removed */

int always_delete_dentry(const struct dentry *dentry)
{
	return 1;
}

const struct dentry_operations simple_dentry_operations = {
	.d_delete = always_delete_dentry,
};

struct dentry *simple_lookup(struct inode *dir, struct dentry *dentry,
			     unsigned int flags)
{
	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);
	/* s_d_op check removed - always NULL (dops never set) */
	d_set_d_op(dentry, &simple_dentry_operations);
	d_add(dentry, NULL);
	return NULL;
}

int dcache_dir_open(struct inode *inode, struct file *file)
{
	file->private_data = d_alloc_cursor(file->f_path.dentry);

	return file->private_data ? 0 : -ENOMEM;
}

int dcache_dir_close(struct inode *inode, struct file *file)
{
	dput(file->private_data);
	return 0;
}

/* scan_positives, dcache_dir_lseek, dcache_readdir removed -
   iterate_shared removed from file_operations */

/* generic_read_dir removed - .read callback no longer exists */

const struct file_operations simple_dir_operations = {
	.open = dcache_dir_open,
	.release = dcache_dir_close,
	/* llseek removed - lseek syscall returns ENOSYS */
	/* read removed - .read callback no longer exists in file_operations */
	/* iterate_shared removed - getdents syscalls return 0 */
	/* fsync removed - fsync syscall returns ENOSYS */
};

static const struct super_operations simple_super_operations = {
	/* statfs removed - statfs syscalls return ENOSYS */
};

static int pseudo_fs_fill_super(struct super_block *s, struct fs_context *fc)
{
	struct pseudo_fs_context *ctx = fc->fs_private;
	struct inode *root;

	s->s_maxbytes = MAX_LFS_FILESIZE;
	s->s_blocksize = PAGE_SIZE;
	s->s_blocksize_bits = PAGE_SHIFT;
	s->s_magic = ctx->magic;
	s->s_op = ctx->ops ?: &simple_super_operations;
	s->s_xattr = ctx->xattr;
	/* s_time_gran removed - field removed */
	root = new_inode(s);
	if (!root)
		return -ENOMEM;

	root->i_ino = 1;
	root->i_mode = S_IFDIR | S_IRUSR | S_IWUSR;
	/* i_atime, i_mtime assignment removed - never read */
	s->s_root = d_make_root(root);
	if (!s->s_root)
		return -ENOMEM;
	s->s_d_op = ctx->dops;
	return 0;
}

static int pseudo_fs_get_tree(struct fs_context *fc)
{
	return get_tree_nodev(fc, pseudo_fs_fill_super);
}

static void pseudo_fs_free(struct fs_context *fc)
{
	kfree(fc->fs_private);
}

static const struct fs_context_operations pseudo_fs_context_ops = {
	.free = pseudo_fs_free,
	.get_tree = pseudo_fs_get_tree,
};

struct pseudo_fs_context *init_pseudo(struct fs_context *fc,
				      unsigned long magic)
{
	struct pseudo_fs_context *ctx;

	ctx = kzalloc(sizeof(struct pseudo_fs_context), GFP_KERNEL);
	if (likely(ctx)) {
		ctx->magic = magic;
		fc->fs_private = ctx;
		fc->ops = &pseudo_fs_context_ops;
		fc->sb_flags |= SB_NOUSER;
		fc->global = true;
	}
	return ctx;
}

/* simple_link, simple_empty, simple_unlink, simple_rmdir, simple_rename removed
   - link/unlink/rmdir/rename syscalls return ENOSYS */

int simple_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		   struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	int error;

	error = setattr_prepare(mnt_userns, dentry, iattr);
	if (error)
		return error;

	if (iattr->ia_valid & ATTR_SIZE)
		truncate_setsize(inode, iattr->ia_size);
	setattr_copy(mnt_userns, inode, iattr);
	/* mark_inode_dirty removed - __mark_inode_dirty is empty stub */
	return 0;
}

static int simple_read_folio(struct file *file, struct folio *folio)
{
	folio_zero_range(folio, 0, folio_size(folio));
	folio_mark_uptodate(folio);
	folio_unlock(folio);
	return 0;
}

int simple_write_begin(struct file *file, struct address_space *mapping,
		       loff_t pos, unsigned len, struct page **pagep,
		       void **fsdata)
{
	struct page *page;
	pgoff_t index;
	unsigned fgp_flags = FGP_LOCK | FGP_WRITE | FGP_CREAT | FGP_STABLE;

	index = pos >> PAGE_SHIFT;

	/* grab_cache_page_write_begin inlined */
	page = pagecache_get_page(mapping, index, fgp_flags,
				  mapping_gfp_mask(mapping));
	if (!page)
		return -ENOMEM;

	*pagep = page;

	if (!PageUptodate(page) && (len != PAGE_SIZE)) {
		unsigned from = pos & (PAGE_SIZE - 1);

		zero_user_segments(page, 0, from, from + len, PAGE_SIZE);
	}
	return 0;
}

static int simple_write_end(struct file *file, struct address_space *mapping,
			    loff_t pos, unsigned len, unsigned copied,
			    struct page *page, void *fsdata)
{
	struct inode *inode = page->mapping->host;
	loff_t last_pos = pos + copied;

	if (!PageUptodate(page)) {
		if (copied < len) {
			unsigned from = pos & (PAGE_SIZE - 1);

			zero_user(page, from + copied, len - copied);
		}
		SetPageUptodate(page);
	}

	if (last_pos > inode->i_size)
		i_size_write(inode, last_pos);

	set_page_dirty(page);
	unlock_page(page);
	put_page(page);

	return copied;
}

const struct address_space_operations ram_aops = {
	.read_folio = simple_read_folio,
	.write_begin = simple_write_begin,
	.write_end = simple_write_end,
};

/* simple_pin_fs, simple_release_fs removed - never called */
/* simple_read_from_buffer, __generic_file_fsync, generic_file_fsync,
 * generic_check_addressable, noop_fsync removed - unused */

/* alloc_anon_inode removed - never called */

static struct dentry *empty_dir_lookup(struct inode *dir, struct dentry *dentry,
				       unsigned int flags)
{
	return ERR_PTR(-ENOENT);
}

/* empty_dir_getattr, empty_dir_listxattr removed - callbacks removed from inode_operations */

static int empty_dir_setattr(struct user_namespace *mnt_userns,
			     struct dentry *dentry, struct iattr *attr)
{
	return -EPERM;
}

static const struct inode_operations empty_dir_inode_operations = {
	.lookup = empty_dir_lookup,
	.permission = generic_permission,
	.setattr = empty_dir_setattr,
	/* getattr, listxattr removed - callbacks removed from inode_operations */
};

/* empty_dir_llseek removed - llseek callback removed from file_operations */

/* empty_dir_readdir removed - iterate_shared removed from file_operations */

static const struct file_operations empty_dir_operations = {
	/* llseek removed - lseek syscall returns ENOSYS */
	/* read removed - .read callback no longer exists in file_operations */
	/* iterate_shared removed - getdents syscalls return 0 */
	/* fsync removed - fsync syscall returns ENOSYS */
};

bool is_empty_dir_inode(struct inode *inode)
{
	return (inode->i_fop == &empty_dir_operations) &&
	       (inode->i_op == &empty_dir_inode_operations);
}
