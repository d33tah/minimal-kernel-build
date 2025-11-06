// SPDX-License-Identifier: GPL-2.0
/* Stubbed attr.c */
#include <linux/fs.h>
#include <linux/export.h>

int setattr_prepare(struct user_namespace *mnt_userns, struct dentry *dentry,
		    struct iattr *attr) { return 0; }
EXPORT_SYMBOL(setattr_prepare);

int inode_newsize_ok(const struct inode *inode, loff_t offset) { return 0; }
EXPORT_SYMBOL(inode_newsize_ok);

void setattr_copy(struct user_namespace *mnt_userns, struct inode *inode,
		  const struct iattr *attr) { }
EXPORT_SYMBOL(setattr_copy);

int may_setattr(struct user_namespace *mnt_userns, struct inode *inode,
		unsigned int ia_valid) { return 0; }
EXPORT_SYMBOL(may_setattr);

int notify_change(struct user_namespace *mnt_userns, struct dentry *dentry,
		  struct iattr *attr, struct inode **delegated_inode) { return 0; }
EXPORT_SYMBOL(notify_change);
