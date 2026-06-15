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

struct xattr {
	const char *name;
	void *value;
	size_t value_len;
};

#endif
