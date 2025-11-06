// SPDX-License-Identifier: GPL-2.0
/* Stubbed d_path.c */
#include <linux/fs.h>
#include <linux/export.h>
#include <linux/dcache.h>
#include <linux/syscalls.h>

char *d_path(const struct path *path, char *buf, int buflen) { return buf; }
EXPORT_SYMBOL(d_path);

char *dentry_path_raw(const struct dentry *dentry, char *buf, int buflen) { return buf; }
EXPORT_SYMBOL(dentry_path_raw);

char *dynamic_dname(struct dentry *dentry, char *buffer, int buflen,
		    const char *fmt, ...) { return buffer; }

char *simple_dname(struct dentry *dentry, char *buffer, int buflen) { return buffer; }

SYSCALL_DEFINE2(getcwd, char __user *, buf, unsigned long, size) { return -ENOSYS; }
