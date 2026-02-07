
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include "internal.h"

/* truncate_inode_pages_final stubbed - Hello World kernel never evicts inodes
 * during its lifetime, so truncation is never needed */
void truncate_inode_pages_final(struct address_space *mapping)
{
}

/* truncate_setsize stubbed - ramfs never truncates during Hello World boot */
void truncate_setsize(struct inode *inode, loff_t newsize)
{
	i_size_write(inode, newsize);
}
