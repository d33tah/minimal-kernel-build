// SPDX-License-Identifier: GPL-2.0
/* Stubbed ioctl.c */
#include <linux/fs.h>
#include <linux/fiemap.h>
#include <linux/export.h>
#include <linux/syscalls.h>

long vfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) { return -ENOTTY; }
EXPORT_SYMBOL(vfs_ioctl);

int fiemap_fill_next_extent(struct fiemap_extent_info *fieinfo, u64 logical,
			     u64 phys, u64 len, u32 flags) { return 1; }
EXPORT_SYMBOL(fiemap_fill_next_extent);

int fiemap_prep(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 *len, u32 supported_flags) { return -EOPNOTSUPP; }
EXPORT_SYMBOL(fiemap_prep);

void fileattr_fill_xflags(struct fileattr *fa, u32 xflags) { }
EXPORT_SYMBOL(fileattr_fill_xflags);

void fileattr_fill_flags(struct fileattr *fa, u32 flags) { }
EXPORT_SYMBOL(fileattr_fill_flags);

int vfs_fileattr_get(struct dentry *dentry, struct fileattr *fa) { return -ENOIOCTLCMD; }
EXPORT_SYMBOL(vfs_fileattr_get);

int copy_fsxattr_to_user(const struct fileattr *fa, struct fsxattr __user *ufa) { return -EFAULT; }
EXPORT_SYMBOL(copy_fsxattr_to_user);

int vfs_fileattr_set(struct user_namespace *mnt_userns, struct dentry *dentry,
		     struct fileattr *fa) { return -ENOIOCTLCMD; }
EXPORT_SYMBOL(vfs_fileattr_set);

SYSCALL_DEFINE3(ioctl, unsigned int, fd, unsigned int, cmd, unsigned long, arg) { return -ENOTTY; }
