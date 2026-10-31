#ifndef _LINUX_NAMEI_H
#define _LINUX_NAMEI_H

#include <linux/fs.h>

enum { LAST_NORM, LAST_ROOT, LAST_DOT, LAST_DOTDOT };

#define LOOKUP_DIRECTORY 0x0002

#define LOOKUP_OPEN 0x0100
#define LOOKUP_CREATE 0x0200
#define LOOKUP_EXCL 0x0400

extern struct dentry *kern_path_create(const char *, struct path *,
				       unsigned int);
extern void done_path_create(struct path *, struct dentry *);

/* try_lookup_one_len, lookup_one_len, lookup_one_len_unlocked, lookup_positive_unlocked,
   lookup_one, lookup_one_unlocked, lookup_one_positive_unlocked removed - no callers */

#endif
