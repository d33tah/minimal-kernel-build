// SPDX-License-Identifier: GPL-2.0-only
#include <linux/export.h>
#include <linux/fs.h>

/* Minimal stub - filesystem stacking not used in minimal kernel */

void fsstack_copy_inode_size(struct inode *dst, struct inode *src)
{
	/* Stubbed */
}
EXPORT_SYMBOL_GPL(fsstack_copy_inode_size);

void fsstack_copy_attr_all(struct inode *dest, const struct inode *src)
{
	/* Stubbed */
}
EXPORT_SYMBOL_GPL(fsstack_copy_attr_all);
