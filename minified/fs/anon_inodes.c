// SPDX-License-Identifier: GPL-2.0-only
/* Stubbed anon_inodes.c */
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/export.h>

struct file *anon_inode_getfile(const char *name,
				const struct file_operations *fops,
				void *priv, int flags) { return ERR_PTR(-ENOMEM); }
EXPORT_SYMBOL_GPL(anon_inode_getfile);

int anon_inode_getfd(const char *name, const struct file_operations *fops,
		     void *priv, int flags) { return -ENOMEM; }
EXPORT_SYMBOL_GPL(anon_inode_getfd);

int anon_inode_getfd_secure(const char *name, const struct file_operations *fops,
			    void *priv, int flags,
			    const struct inode *context_inode) { return -ENOMEM; }
EXPORT_SYMBOL_GPL(anon_inode_getfd_secure);

int __init anon_inode_init(void) { return 0; }
