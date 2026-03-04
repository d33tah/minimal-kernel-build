#include <linux/pagemap.h>

struct dentry *simple_lookup(struct inode *dir, struct dentry *dentry,
			     unsigned int flags)
{
	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENAMETOOLONG);
	d_add(dentry, NULL);
	return NULL;
}

const struct file_operations simple_dir_operations = {};

static int simple_read_folio(struct file *file, struct folio *folio)
{
	folio_zero_range(folio, 0, folio_size(folio));
	folio_mark_uptodate(folio);
	folio_unlock(folio);
	return 0;
}

const struct address_space_operations ram_aops = {
	.read_folio = simple_read_folio,
};
