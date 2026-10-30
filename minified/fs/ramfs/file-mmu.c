

#include <linux/fs.h>
#include <linux/ramfs.h>

#include "internal.h"

const struct file_operations ramfs_file_operations = {
	.read_iter = generic_file_read_iter,
	.write_iter = generic_file_write_iter,
	.mmap = generic_file_mmap,
};

const struct inode_operations ramfs_file_inode_operations = {};
