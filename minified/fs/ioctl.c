#include <linux/fs.h>
#include <linux/fiemap.h>
#include <linux/export.h>
#include <linux/syscalls.h>

long vfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) { return -ENOTTY; }

int fiemap_fill_next_extent(struct fiemap_extent_info *fieinfo, u64 logical,
			     u64 phys, u64 len, u32 flags) { return 1; }

int fiemap_prep(struct inode *inode, struct fiemap_extent_info *fieinfo,
		u64 start, u64 *len, u32 supported_flags) { return -EOPNOTSUPP; }

void fileattr_fill_xflags(struct fileattr *fa, u32 xflags) { }

void fileattr_fill_flags(struct fileattr *fa, u32 flags) { }

int vfs_fileattr_get(struct dentry *dentry, struct fileattr *fa) { return -ENOIOCTLCMD; }

int copy_fsxattr_to_user(const struct fileattr *fa, struct fsxattr __user *ufa) { return -EFAULT; }

int vfs_fileattr_set(struct user_namespace *mnt_userns, struct dentry *dentry,
		     struct fileattr *fa) { return -ENOIOCTLCMD; }

SYSCALL_DEFINE3(ioctl, unsigned int, fd, unsigned int, cmd, unsigned long, arg) { return -ENOTTY; }
