#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/syscalls.h>
#include <linux/export.h>

void generic_fillattr(struct user_namespace *mnt_userns, struct inode *inode,
		      struct kstat *stat) { }

void generic_fill_statx_attr(struct inode *inode, struct kstat *stat) { }

int vfs_getattr_nosec(const struct path *path, struct kstat *stat,
		      u32 request_mask, unsigned int query_flags) { return 0; }

int vfs_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int query_flags) { return 0; }

void __inode_add_bytes(struct inode *inode, loff_t bytes) { }

void inode_add_bytes(struct inode *inode, loff_t bytes) { }

void __inode_sub_bytes(struct inode *inode, loff_t bytes) { }

void inode_sub_bytes(struct inode *inode, loff_t bytes) { }

loff_t inode_get_bytes(struct inode *inode) { return 0; }

void inode_set_bytes(struct inode *inode, loff_t bytes) { }

SYSCALL_DEFINE2(stat, const char __user *, filename,
		struct __old_kernel_stat __user *, statbuf) { return -ENOSYS; }
SYSCALL_DEFINE2(lstat, const char __user *, filename,
		struct __old_kernel_stat __user *, statbuf) { return -ENOSYS; }
SYSCALL_DEFINE2(fstat, unsigned int, fd,
		struct __old_kernel_stat __user *, statbuf) { return -ENOSYS; }
SYSCALL_DEFINE2(newstat, const char __user *, filename,
		struct stat __user *, statbuf) { return -ENOSYS; }
SYSCALL_DEFINE2(newlstat, const char __user *, filename,
		struct stat __user *, statbuf) { return -ENOSYS; }
SYSCALL_DEFINE4(newfstatat, int, dfd, const char __user *, filename,
		struct stat __user *, statbuf, int, flag) { return -ENOSYS; }
SYSCALL_DEFINE2(newfstat, unsigned int, fd,
		struct stat __user *, statbuf) { return -ENOSYS; }
SYSCALL_DEFINE4(readlinkat, int, dfd, const char __user *, pathname,
		char __user *, buf, int, bufsiz) { return -EINVAL; }
SYSCALL_DEFINE3(readlink, const char __user *, path,
		char __user *, buf, int, bufsiz) { return -EINVAL; }
SYSCALL_DEFINE2(stat64, const char __user *, filename,
		struct stat64 __user *, statbuf) { return -ENOSYS; }
SYSCALL_DEFINE2(lstat64, const char __user *, filename,
		struct stat64 __user *, statbuf) { return -ENOSYS; }
SYSCALL_DEFINE2(fstat64, unsigned long, fd,
		struct stat64 __user *, statbuf) { return -ENOSYS; }
SYSCALL_DEFINE4(fstatat64, int, dfd, const char __user *, filename,
		struct stat64 __user *, statbuf, int, flag) { return -ENOSYS; }
SYSCALL_DEFINE5(statx, int, dfd, const char __user *, filename, unsigned, flags,
		unsigned int, mask, struct statx __user *, buffer) { return -ENOSYS; }
