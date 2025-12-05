/* Stubs for bad inode handling */
#include <linux/fs.h>

void make_bad_inode(struct inode *inode)
{
	 
}

bool is_bad_inode(struct inode *inode)
{
	return false;
}

void iget_failed(struct inode *inode)
{
	 
}
