// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/bad_inode.c - stub implementation
 */

#include <linux/fs.h>
#include <linux/export.h>

void make_bad_inode(struct inode *inode)
{
	/* Stub - do nothing */
}

bool is_bad_inode(struct inode *inode)
{
	return false;
}

void iget_failed(struct inode *inode)
{
	/* Stub - do nothing */
}
