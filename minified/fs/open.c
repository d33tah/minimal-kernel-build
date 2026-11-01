// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/open.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/namei.h>

#include "internal.h"

static int do_dentry_open(struct file *f, struct inode *inode,
			  int (*open)(struct inode *, struct file *))
{
	int error;

	path_get(&f->f_path);
	f->f_inode = inode;
	f->f_mapping = inode->i_mapping;

	if (f->f_mode & FMODE_WRITE && !special_file(inode->i_mode)) {
		error = get_write_access(inode);
		if (unlikely(error))
			goto cleanup_file;
		f->f_mode |= FMODE_WRITER;
	}

	f->f_op = fops_get(inode->i_fop);
	if (WARN_ON(!f->f_op)) {
		error = -ENODEV;
		goto cleanup_all;
	}

	if (!open)
		open = f->f_op->open;
	if (open) {
		error = open(inode, f);
		if (error)
			goto cleanup_all;
	}
	f->f_mode |= FMODE_OPENED;
	if ((f->f_mode & FMODE_READ) && likely(f->f_op->read_iter))
		f->f_mode |= FMODE_CAN_READ;
	if ((f->f_mode & FMODE_WRITE) && likely(f->f_op->write_iter))
		f->f_mode |= FMODE_CAN_WRITE;
	return 0;

cleanup_all:
	if (WARN_ON_ONCE(error > 0))
		error = -EINVAL;
	if (f->f_mode & FMODE_WRITER)
		put_write_access(inode);
cleanup_file:
	path_put(&f->f_path);
	f->f_path.mnt = NULL;
	f->f_path.dentry = NULL;
	f->f_inode = NULL;
	return error;
}

/**
 * vfs_open - open the file at the given path
 * @path: path to open
 * @file: newly allocated file with f_flag initialized
 * @cred: credentials to use
 */
int vfs_open(const struct path *path, struct file *file)
{
	file->f_path = *path;
	return do_dentry_open(file, d_backing_inode(path->dentry), NULL);
}

/*
 * In-kernel opens only (init's /dev/console O_RDWR and the initramfs
 * O_WRONLY|O_CREAT files): there is no open(2), so there is no O_PATH,
 * O_TMPFILE, O_TRUNC, O_APPEND, O_DIRECTORY or O_NOFOLLOW handling.
 */
struct file *filp_open(const char *filename, int flags, umode_t mode)
{
	struct filename *name = getname_kernel(filename);
	struct open_flags op = {
		.open_flag = flags,
		.acc_mode = ACC_MODE(flags),
	};
	struct file *file;

	if (IS_ERR(name))
		return ERR_CAST(name);
	if (flags & O_CREAT)
		op.mode = (mode & S_IALLUGO) | S_IFREG;
	file = do_filp_open(name, &op);
	putname(name);
	return file;
}
