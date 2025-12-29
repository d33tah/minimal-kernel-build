// SPDX-License-Identifier: GPL-2.0
/* Minimal ramfs NOMMU file operations stub */

#include <linux/fs.h>
#include <linux/mm.h>

/* NOMMU ramfs file operations - minimal stub */
const struct file_operations ramfs_file_operations = {
	.read_iter = generic_file_read_iter,
	.write_iter = generic_file_write_iter,
	.llseek = generic_file_llseek,
};

/* ramfs inode operations */
const struct inode_operations ramfs_file_inode_operations = {
	.setattr = simple_setattr,
	.getattr = simple_getattr,
};
