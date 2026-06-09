
#include <linux/blkdev.h>
#include <linux/export.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/mount.h>
#include <linux/statfs.h> /* vfs.h redirect */

#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/writeback.h>

#include <linux/fs_context.h>

#include <linux/uaccess.h>

#include "internal.h"

int simple_getattr(struct user_namespace *mnt_userns, const struct path *path,
		   struct kstat *stat, u32 request_mask,
		   unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	generic_fillattr(&init_user_ns, inode, stat);
	stat->blocks = inode->i_mapping->nrpages << (PAGE_SHIFT - 9);
	return 0;
}

int simple_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	buf->f_type = dentry->d_sb->s_magic;
	buf->f_bsize = PAGE_SIZE;
	buf->f_namelen = NAME_MAX;
	return 0;
}

int always_delete_dentry(const struct dentry *dentry)
{
	return 1;
}

const struct dentry_operations simple_dentry_operations = {
	.d_delete = always_delete_dentry,
};

struct dentry *simple_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);
	if (!dentry->d_sb->s_d_op)
		d_set_d_op(dentry, &simple_dentry_operations);
	d_add(dentry, NULL);
	return NULL;
}

int simple_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);

	inode->i_ctime = dir->i_ctime = dir->i_mtime = current_time(inode);
	inc_nlink(inode);
	ihold(inode);
	dget(dentry);
	d_instantiate(dentry, inode);
	return 0;
}

int simple_empty(struct dentry *dentry)
{
	struct dentry *child;
	int ret = 0;

	spin_lock(&dentry->d_lock);
	list_for_each_entry(child, &dentry->d_subdirs, d_child) {
		spin_lock_nested(&child->d_lock, DENTRY_D_LOCK_NESTED);
		if (simple_positive(child)) {
			spin_unlock(&child->d_lock);
			goto out;
		}
		spin_unlock(&child->d_lock);
	}
	ret = 1;
out:
	spin_unlock(&dentry->d_lock);
	return ret;
}

int simple_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	inode->i_ctime = dir->i_ctime = dir->i_mtime = current_time(inode);
	drop_nlink(inode);
	dput(dentry);
	return 0;
}

int simple_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (!simple_empty(dentry))
		return -ENOTEMPTY;

	drop_nlink(d_inode(dentry));
	simple_unlink(dir, dentry);
	drop_nlink(dir);
	return 0;
}

/* Stub: simple_rename_exchange only used internally */
static int simple_rename_exchange(struct inode *old_dir, struct dentry *old_dentry,
			   struct inode *new_dir, struct dentry *new_dentry) { return -EINVAL; }

int simple_rename(struct user_namespace *mnt_userns, struct inode *old_dir,
		  struct dentry *old_dentry, struct inode *new_dir,
		  struct dentry *new_dentry, unsigned int flags)
{
	struct inode *inode = d_inode(old_dentry);
	int they_are_dirs = d_is_dir(old_dentry);

	if (flags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE))
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return simple_rename_exchange(old_dir, old_dentry, new_dir, new_dentry);

	if (!simple_empty(new_dentry))
		return -ENOTEMPTY;

	if (d_really_is_positive(new_dentry)) {
		simple_unlink(new_dir, new_dentry);
		if (they_are_dirs) {
			drop_nlink(d_inode(new_dentry));
			drop_nlink(old_dir);
		}
	} else if (they_are_dirs) {
		drop_nlink(old_dir);
		inc_nlink(new_dir);
	}

	old_dir->i_ctime = old_dir->i_mtime = new_dir->i_ctime =
		new_dir->i_mtime = inode->i_ctime = current_time(old_dir);

	return 0;
}

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
	mark_inode_dirty(inode);
	return 0;
}

static int simple_read_folio(struct file *file, struct folio *folio)
{
	folio_zero_range(folio, 0, folio_size(folio));
	flush_dcache_folio(folio);
	folio_mark_uptodate(folio);
	folio_unlock(folio);
	return 0;
}

int simple_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata)
{
	struct page *page;
	pgoff_t index;

	index = pos >> PAGE_SHIFT;

	page = grab_cache_page_write_begin(mapping, index);
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
	.read_folio	= simple_read_folio,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	.dirty_folio	= noop_dirty_folio,
};

/* simple_read_from_buffer, __generic_file_fsync, generic_file_fsync,
 * generic_check_addressable removed - unused */

int noop_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	return 0;
}


