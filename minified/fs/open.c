
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/tty.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include <linux/init_task.h> /* for securebits defines */
#include <linux/mount.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/pagemap.h>

#include <linux/fs_struct.h>

#include <linux/compat.h>
#include <linux/mnt_idmapping.h>

#include "internal.h"

int do_truncate(struct user_namespace *mnt_userns, struct dentry *dentry,
		loff_t length, unsigned int time_attrs, struct file *filp)
{
	int ret;
	struct iattr newattrs;

	 
	if (length < 0)
		return -EINVAL;

	newattrs.ia_size = length;
	newattrs.ia_valid = ATTR_SIZE | time_attrs;
	/* ATTR_FILE / ia_file are never read on this build */

	/* dentry_needs_remove_privs() was a constant 0 on this build. */

	inode_lock(dentry->d_inode);
	 
	ret = notify_change(mnt_userns, dentry, &newattrs, NULL);
	inode_unlock(dentry->d_inode);
	return ret;
}

long vfs_truncate(const struct path *path, loff_t length)
{
	struct user_namespace *mnt_userns;
	struct inode *inode;
	long error;

	inode = path->dentry->d_inode;

	 
	if (S_ISDIR(inode->i_mode))
		return -EISDIR;
	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	error = mnt_want_write(path->mnt);
	if (error)
		goto out;

	mnt_userns = mnt_user_ns(path->mnt);
	error = inode_permission(mnt_userns, inode, MAY_WRITE);
	if (error)
		goto mnt_drop_write_and_out;

	error = get_write_access(inode);
	if (error)
		goto mnt_drop_write_and_out;

	error = do_truncate(mnt_userns, path->dentry, length, 0, NULL);

	put_write_access(inode);
mnt_drop_write_and_out:
	mnt_drop_write(path->mnt);
out:
	return error;
}

int chmod_common(const struct path *path, umode_t mode)
{
	/* Stub: chmod not needed for minimal kernel */
	return -EOPNOTSUPP;
}

int vfs_fchmod(struct file *file, umode_t mode)
{
	return chmod_common(&file->f_path, mode);
}

int chown_common(const struct path *path, uid_t user, gid_t group)
{
	/* Stub: chown not needed for minimal kernel */
	return -EOPNOTSUPP;
}

int vfs_fchown(struct file *file, uid_t user, gid_t group)
{
	return -ENOSYS;
}

static int do_dentry_open(struct file *f,
			  struct inode *inode,
			  int (*open)(struct inode *, struct file *))
{
	static const struct file_operations empty_fops = {};
	int error;

	path_get(&f->f_path);
	f->f_inode = inode;
	f->f_mapping = inode->i_mapping;

	if (unlikely(f->f_flags & O_PATH)) {
		f->f_mode = FMODE_PATH | FMODE_OPENED;
		f->f_op = &empty_fops;
		return 0;
	}

	if (f->f_mode & FMODE_WRITE && !special_file(inode->i_mode)) {
		error = get_write_access(inode);
		if (unlikely(error))
			goto cleanup_file;
		error = __mnt_want_write(f->f_path.mnt);
		if (unlikely(error)) {
			put_write_access(inode);
			goto cleanup_file;
		}
		f->f_mode |= FMODE_WRITER;
	}

	 
	if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode))
		f->f_mode |= FMODE_ATOMIC_POS;

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
	if ((f->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		i_readcount_inc(inode);
	if ((f->f_mode & FMODE_READ) &&
	     likely(f->f_op->read || f->f_op->read_iter))
		f->f_mode |= FMODE_CAN_READ;
	if ((f->f_mode & FMODE_WRITE) &&
	     likely(f->f_op->write || f->f_op->write_iter))
		f->f_mode |= FMODE_CAN_WRITE;
	/*
	 * No address_space_operations on this build sets ->direct_IO, so
	 * O_DIRECT is always rejected below.
	 */
	f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	if (f->f_flags & O_DIRECT)
		return -EINVAL;

	return 0;

cleanup_all:
	if (WARN_ON_ONCE(error > 0))
		error = -EINVAL;
	fops_put(f->f_op);
	if (f->f_mode & FMODE_WRITER) {
		put_write_access(inode);
		__mnt_drop_write(f->f_path.mnt);
	}
cleanup_file:
	path_put(&f->f_path);
	f->f_path.mnt = NULL;
	f->f_path.dentry = NULL;
	f->f_inode = NULL;
	return error;
}

int vfs_open(const struct path *path, struct file *file)
{
	file->f_path = *path;
	return do_dentry_open(file, d_backing_inode(path->dentry), NULL);
}

#define WILL_CREATE(flags)	(flags & (O_CREAT | __O_TMPFILE))
#define O_PATH_FLAGS		(O_DIRECTORY | O_NOFOLLOW | O_PATH | O_CLOEXEC)

inline int build_open_flags(const struct open_how *how, struct open_flags *op)
{
	u64 flags = how->flags;
	u64 strip = FMODE_NONOTIFY | O_CLOEXEC;
	int lookup_flags = 0;
	int acc_mode = ACC_MODE(flags);

	BUILD_BUG_ON_MSG(upper_32_bits(VALID_OPEN_FLAGS),
			 "struct open_flags doesn't yet handle flags > 32 bits");

	 
	flags &= ~strip;

	 
	if (flags & ~VALID_OPEN_FLAGS)
		return -EINVAL;
	if (how->resolve & ~VALID_RESOLVE_FLAGS)
		return -EINVAL;

	 
	if ((how->resolve & RESOLVE_BENEATH) && (how->resolve & RESOLVE_IN_ROOT))
		return -EINVAL;

	 
	if (WILL_CREATE(flags)) {
		if (how->mode & ~S_IALLUGO)
			return -EINVAL;
		op->mode = how->mode | S_IFREG;
	} else {
		if (how->mode != 0)
			return -EINVAL;
		op->mode = 0;
	}

	 
	if (flags & __O_TMPFILE) {
		if ((flags & O_TMPFILE_MASK) != O_TMPFILE)
			return -EINVAL;
		if (!(acc_mode & MAY_WRITE))
			return -EINVAL;
	}
	if (flags & O_PATH) {
		 
		if (flags & ~O_PATH_FLAGS)
			return -EINVAL;
		acc_mode = 0;
	}

	 
	if (flags & __O_SYNC)
		flags |= O_DSYNC;

	op->open_flag = flags;

	 
	if (flags & O_TRUNC)
		acc_mode |= MAY_WRITE;

	 
	if (flags & O_APPEND)
		acc_mode |= MAY_APPEND;

	op->acc_mode = acc_mode;

	op->intent = flags & O_PATH ? 0 : LOOKUP_OPEN;

	if (flags & O_CREAT) {
		op->intent |= LOOKUP_CREATE;
		if (flags & O_EXCL) {
			op->intent |= LOOKUP_EXCL;
			flags |= O_NOFOLLOW;
		}
	}

	if (flags & O_DIRECTORY)
		lookup_flags |= LOOKUP_DIRECTORY;
	if (!(flags & O_NOFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;

	if (how->resolve & RESOLVE_NO_XDEV)
		lookup_flags |= LOOKUP_NO_XDEV;
	if (how->resolve & RESOLVE_NO_MAGICLINKS)
		lookup_flags |= LOOKUP_NO_MAGICLINKS;
	if (how->resolve & RESOLVE_NO_SYMLINKS)
		lookup_flags |= LOOKUP_NO_SYMLINKS;
	if (how->resolve & RESOLVE_BENEATH)
		lookup_flags |= LOOKUP_BENEATH;
	if (how->resolve & RESOLVE_IN_ROOT)
		lookup_flags |= LOOKUP_IN_ROOT;
	if (how->resolve & RESOLVE_CACHED) {
		 
		if (flags & (O_TRUNC | O_CREAT | O_TMPFILE))
			return -EAGAIN;
		lookup_flags |= LOOKUP_CACHED;
	}

	op->lookup_flags = lookup_flags;
	return 0;
}

struct file *file_open_name(struct filename *name, int flags, umode_t mode)
{
	struct open_flags op;
	struct open_how how = {
		.flags = flags & VALID_OPEN_FLAGS,
		.mode = mode & S_IALLUGO,
	};
	int err;

	if (how.flags & O_PATH)
		how.flags &= O_PATH_FLAGS;
	if (!WILL_CREATE(how.flags))
		how.mode = 0;

	err = build_open_flags(&how, &op);
	if (err)
		return ERR_PTR(err);
	return do_filp_open(AT_FDCWD, name, &op);
}

struct file *filp_open(const char *filename, int flags, umode_t mode)
{
	struct filename *name = getname_kernel(filename);
	struct file *file = ERR_CAST(name);
	
	if (!IS_ERR(name)) {
		file = file_open_name(name, flags, mode);
		putname(name);
	}
	return file;
}


int filp_close(struct file *filp, fl_owner_t id)
{
	int retval = 0;

	if (!file_count(filp)) {
		printk(KERN_ERR "VFS: Close: file count is 0\n");
		return 0;
	}

	/* No live file_operations sets ->flush. */
	fput(filp);
	return retval;
}


int nonseekable_open(struct inode *inode, struct file *filp)
{
	return 0;
}

/* Removed: stream_open - never called */

