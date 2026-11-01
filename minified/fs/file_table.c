// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/file_table.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/fs.h>
#include <linux/file.h>
#include "internal.h"

static void file_free_rcu(struct rcu_head *head)
{
	struct file *f = container_of(head, struct file, f_u.fu_rcuhead);

	kfree(f);
}

static inline void file_free(struct file *f)
{
	/* One CPU, no preemption, no lockless fd lookup: free at once. */
	file_free_rcu(&f->f_u.fu_rcuhead);
}

static struct file *__alloc_file(int flags, const struct cred *cred)
{
	struct file *f;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (unlikely(!f))
		return ERR_PTR(-ENOMEM);

	atomic_long_set(&f->f_count, 1);
	f->f_mode = OPEN_FMODE(flags);

	/* f->f_version: 0 */

	return f;
}

/* Find an unused file structure and return a pointer to it.
 * Returns an error pointer if some error happend e.g. we over file
 * structures limit, run out of memory or operation is not permitted.
 *
 * Be very careful using this.  You are responsible for
 * getting write access to any mount that you might assign
 * to this filp, if it is opened for write.  If this is not
 * done, you will imbalance int the mount's writer count
 * and a warning at __fput() time.
 */
struct file *alloc_empty_file(int flags, const struct cred *cred)
{
	struct file *f;

	f = __alloc_file(flags, cred);

	return f;
}

/* the real guts of fput() - releasing the last reference to file
 */
static void __fput(struct file *file)
{
	struct dentry *dentry = file->f_path.dentry;
	struct inode *inode = file->f_inode;
	fmode_t mode = file->f_mode;

	if (unlikely(!(file->f_mode & FMODE_OPENED)))
		goto out;

	if (mode & FMODE_WRITER)
		put_write_access(inode);
	dput(dentry);
out:
	file_free(file);
}

void fput(struct file *file)
{
	if (atomic_long_dec_and_test(&file->f_count))
		__fput(file);
}
