#ifndef _LINUX_RAMFS_H
#define _LINUX_RAMFS_H

#include <linux/fs_context.h>
extern int ramfs_init_fs_context(struct fs_context *fc);

extern const struct file_operations ramfs_file_operations;

#endif
