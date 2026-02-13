#include <linux/pagemap.h>
#include <linux/cred.h>
#include <linux/mount.h>
#include <linux/fs_context.h>

#include "internal.h"

/* always_delete_dentry, simple_dentry_operations removed -
   d_op never read, DCACHE_OP_DELETE never tested */

struct dentry *simple_lookup(struct inode *dir, struct dentry *dentry,
			     unsigned int flags)
{
	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);
	d_add(dentry, NULL);
	return NULL;
}

static int dcache_dir_open(struct inode *inode, struct file *file)
{
	return -ENOMEM;
}

int dcache_dir_close(struct inode *inode, struct file *file)
{
	return 0;
}

/* scan_positives, dcache_dir_lseek, dcache_readdir removed -
   iterate_shared removed from file_operations */

const struct file_operations simple_dir_operations = {
	.open = dcache_dir_open,
	.release = dcache_dir_close,
};

/* init_pseudo, pseudo_fs_fill_super, pseudo_fs_get_tree, pseudo_fs_free,
   pseudo_fs_context_ops removed - init_pseudo never called (~55 LOC) */

/* simple_link, simple_empty, simple_unlink, simple_rmdir, simple_rename removed
   - link/unlink/rmdir/rename syscalls return ENOSYS */

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

/* simple_read_from_buffer, __generic_file_fsync, generic_file_fsync,
 * generic_check_addressable, noop_fsync removed - unused */

/* empty_dir_lookup, empty_dir_setattr, empty_dir_inode_operations,
 * empty_dir_operations, is_empty_dir_inode removed - never used */
