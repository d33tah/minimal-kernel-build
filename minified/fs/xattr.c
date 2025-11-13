// SPDX-License-Identifier: GPL-2.0-only
/* Stubbed xattr.c */
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/export.h>
#include <linux/syscalls.h>

int xattr_supported_namespace(struct inode *inode, const char *prefix) { return 0; }

int __vfs_setxattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		   struct inode *inode, const char *name, const void *value,
		   size_t size, int flags) { return -EOPNOTSUPP; }

int __vfs_setxattr_locked(struct user_namespace *mnt_userns, struct dentry *dentry,
			  const char *name, const void *value, size_t size,
			  int flags, struct inode **delegated_inode) { return -EOPNOTSUPP; }

int vfs_setxattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		 const char *name, const void *value, size_t size, int flags) { return -EOPNOTSUPP; }

ssize_t __vfs_getxattr(struct dentry *dentry, struct inode *inode,
		       const char *name, void *value, size_t size) { return -EOPNOTSUPP; }

ssize_t vfs_getxattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		     const char *name, void *value, size_t size) { return -EOPNOTSUPP; }

ssize_t vfs_listxattr(struct dentry *d, char *list, size_t size) { return -EOPNOTSUPP; }

int __vfs_removexattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		      const char *name) { return -EOPNOTSUPP; }

int __vfs_removexattr_locked(struct user_namespace *mnt_userns, struct dentry *dentry,
			     const char *name, struct inode **delegated_inode) { return -EOPNOTSUPP; }

int vfs_removexattr(struct user_namespace *mnt_userns, struct dentry *dentry,
		    const char *name) { return -EOPNOTSUPP; }

ssize_t generic_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size) { return -EOPNOTSUPP; }

const char *xattr_full_name(const struct xattr_handler *handler, const char *name) { return NULL; }

ssize_t vfs_getxattr_alloc(struct user_namespace *mnt_userns, struct dentry *dentry,
			   const char *name, char **xattr_value, size_t size, gfp_t flags) { return -EOPNOTSUPP; }

SYSCALL_DEFINE5(setxattr, const char __user *, pathname, const char __user *, name,
		const void __user *, value, size_t, size, int, flags) { return -EOPNOTSUPP; }
SYSCALL_DEFINE5(lsetxattr, const char __user *, pathname, const char __user *, name,
		const void __user *, value, size_t, size, int, flags) { return -EOPNOTSUPP; }
SYSCALL_DEFINE5(fsetxattr, int, fd, const char __user *, name,
		const void __user *, value, size_t, size, int, flags) { return -EOPNOTSUPP; }
SYSCALL_DEFINE4(getxattr, const char __user *, pathname, const char __user *, name,
		void __user *, value, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE4(lgetxattr, const char __user *, pathname, const char __user *, name,
		void __user *, value, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE4(fgetxattr, int, fd, const char __user *, name,
		void __user *, value, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE3(listxattr, const char __user *, pathname, char __user *, list, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE3(llistxattr, const char __user *, pathname, char __user *, list, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE3(flistxattr, int, fd, char __user *, list, size_t, size) { return -EOPNOTSUPP; }
SYSCALL_DEFINE2(removexattr, const char __user *, pathname, const char __user *, name) { return -EOPNOTSUPP; }
SYSCALL_DEFINE2(lremovexattr, const char __user *, pathname, const char __user *, name) { return -EOPNOTSUPP; }
SYSCALL_DEFINE2(fremovexattr, int, fd, const char __user *, name) { return -EOPNOTSUPP; }
