#ifndef _LINUX_RAMFS_H
#define _LINUX_RAMFS_H
#include <linux/fs_parser.h>
struct inode *ramfs_get_inode(struct super_block *sb, const struct inode *dir, umode_t mode, dev_t dev);
extern int ramfs_init_fs_context(struct fs_context *fc);
extern const struct fs_parameter_spec ramfs_fs_parameters[];
extern const struct file_operations ramfs_file_operations;
extern const struct vm_operations_struct generic_file_vm_ops;
#endif
