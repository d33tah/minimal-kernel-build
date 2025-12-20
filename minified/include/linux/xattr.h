#ifndef _LINUX_XATTR_H
#define _LINUX_XATTR_H


#include <linux/slab.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/user_namespace.h>

struct inode;
struct dentry;

struct xattr_handler {
	const char *name;
	const char *prefix;
	int flags;       
	bool (*list)(struct dentry *dentry);
	int (*get)(const struct xattr_handler *, struct dentry *dentry,
		   struct inode *inode, const char *name, void *buffer,
		   size_t size);
	int (*set)(const struct xattr_handler *,
		   struct user_namespace *mnt_userns, struct dentry *dentry,
		   struct inode *inode, const char *name, const void *buffer,
		   size_t size, int flags);
};

const char *xattr_full_name(const struct xattr_handler *, const char *);

struct xattr {
	const char *name;
	void *value;
	size_t value_len;
};

ssize_t __vfs_getxattr(struct dentry *, struct inode *, const char *, void *, size_t);
ssize_t vfs_getxattr(struct user_namespace *, struct dentry *, const char *,
		     void *, size_t);
ssize_t vfs_listxattr(struct dentry *d, char *list, size_t size);
int __vfs_setxattr(struct user_namespace *, struct dentry *, struct inode *,
		   const char *, const void *, size_t, int);
int __vfs_setxattr_noperm(struct user_namespace *, struct dentry *,
			  const char *, const void *, size_t, int);
int __vfs_setxattr_locked(struct user_namespace *, struct dentry *,
			  const char *, const void *, size_t, int,
			  struct inode **);
int vfs_setxattr(struct user_namespace *, struct dentry *, const char *,
		 const void *, size_t, int);
int __vfs_removexattr(struct user_namespace *, struct dentry *, const char *);
int __vfs_removexattr_locked(struct user_namespace *, struct dentry *,
			     const char *, struct inode **);
int vfs_removexattr(struct user_namespace *, struct dentry *, const char *);

ssize_t generic_listxattr(struct dentry *dentry, char *buffer, size_t buffer_size);
ssize_t vfs_getxattr_alloc(struct user_namespace *mnt_userns,
			   struct dentry *dentry, const char *name,
			   char **xattr_value, size_t size, gfp_t flags);

int xattr_supported_namespace(struct inode *inode, const char *prefix);

/* xattr_prefix removed - never called */

struct simple_xattrs {
	struct list_head head;
	spinlock_t lock;
};

/* simple_xattrs_init removed - never called */
/* simple_xattr_alloc, simple_xattr_get/set/list/list_add removed - unused */

#endif	 
