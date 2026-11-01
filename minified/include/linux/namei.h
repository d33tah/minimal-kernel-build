/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H

#include <linux/fs.h>

/*
 * Type of the last component on LOOKUP_PARENT
 */
enum { LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT };

#define LOOKUP_DIRECTORY 0x0002 /* require a directory */

#define LOOKUP_CREATE 0x0200 /* ... in object creation */
#define LOOKUP_EXCL 0x0400 /* ... in exclusive creation */

extern struct dentry *kern_path_create(const char *, struct path *,
				       unsigned int);
extern void done_path_create(struct path *, struct dentry *);

#endif /* _LINUX_NAMEI_H */
