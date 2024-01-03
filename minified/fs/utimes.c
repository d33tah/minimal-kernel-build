// SPDX-License-Identifier: GPL-2.0
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/utime.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <asm/unistd.h>

static bool nsec_valid(long nsec)
{
	if (nsec == UTIME_OMIT || nsec == UTIME_NOW)
		return true;

	return nsec >= 0 && nsec <= 999999999;
}

int vfs_utimes(const struct path *path, struct timespec64 *times)
{
	int error;
	struct iattr newattrs;
	struct inode *inode = path->dentry->d_inode;
	struct inode *delegated_inode = NULL;

	if (times) {
		if (!nsec_valid(times[0].tv_nsec) ||
		    !nsec_valid(times[1].tv_nsec))
			return -EINVAL;
		if (times[0].tv_nsec == UTIME_NOW &&
		    times[1].tv_nsec == UTIME_NOW)
			times = NULL;
	}

	error = mnt_want_write(path->mnt);
	if (error)
		goto out;

	newattrs.ia_valid = ATTR_CTIME | ATTR_MTIME | ATTR_ATIME;
	if (times) {
		if (times[0].tv_nsec == UTIME_OMIT)
			newattrs.ia_valid &= ~ATTR_ATIME;
		else if (times[0].tv_nsec != UTIME_NOW) {
			newattrs.ia_atime = times[0];
			newattrs.ia_valid |= ATTR_ATIME_SET;
		}

		if (times[1].tv_nsec == UTIME_OMIT)
			newattrs.ia_valid &= ~ATTR_MTIME;
		else if (times[1].tv_nsec != UTIME_NOW) {
			newattrs.ia_mtime = times[1];
			newattrs.ia_valid |= ATTR_MTIME_SET;
		}
		/*
		 * Tell setattr_prepare(), that this is an explicit time
		 * update, even if neither ATTR_ATIME_SET nor ATTR_MTIME_SET
		 * were used.
		 */
		newattrs.ia_valid |= ATTR_TIMES_SET;
	} else {
		newattrs.ia_valid |= ATTR_TOUCH;
	}
retry_deleg:
	inode_lock(inode);
	error = notify_change(mnt_user_ns(path->mnt), path->dentry, &newattrs,
			      &delegated_inode);
	inode_unlock(inode);
	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}

	mnt_drop_write(path->mnt);
out:
	return error;
}

static int do_utimes_path(int dfd, const char __user *filename,
		struct timespec64 *times, int flags)
{
	struct path path;
	int lookup_flags = 0, error;

	if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH))
		return -EINVAL;

	if (!(flags & AT_SYMLINK_NOFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;
	if (flags & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;

retry:
	error = user_path_at(dfd, filename, lookup_flags, &path);
	if (error)
		return error;

	error = vfs_utimes(&path, times);
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}

	return error;
}

static int do_utimes_fd(int fd, struct timespec64 *times, int flags)
{
	struct fd f;
	int error;

	if (flags)
		return -EINVAL;

	f = fdget(fd);
	if (!f.file)
		return -EBADF;
	error = vfs_utimes(&f.file->f_path, times);
	fdput(f);
	return error;
}

/*
 * do_utimes - change times on filename or file descriptor
 * @dfd: open file descriptor, -1 or AT_FDCWD
 * @filename: path name or NULL
 * @times: new times or NULL
 * @flags: zero or more flags (only AT_SYMLINK_NOFOLLOW for the moment)
 *
 * If filename is NULL and dfd refers to an open file, then operate on
 * the file.  Otherwise look up filename, possibly using dfd as a
 * starting point.
 *
 * If times==NULL, set access and modification to current time,
 * must be owner or have write permission.
 * Else, update from *times, must be owner or super user.
 */
long do_utimes(int dfd, const char __user *filename, struct timespec64 *times,
	       int flags)
{
	if (filename == NULL && dfd != AT_FDCWD)
		return do_utimes_fd(dfd, times, flags);
	return do_utimes_path(dfd, filename, times, flags);
}

SYSCALL_DEFINE4(utimensat, int, dfd, const char __user *, filename,
		struct __kernel_timespec __user *, utimes, int, flags)
{
	struct timespec64 tstimes[2];

	if (utimes) {
		if ((get_timespec64(&tstimes[0], &utimes[0]) ||
			get_timespec64(&tstimes[1], &utimes[1])))
			return -EFAULT;

		/* Nothing to do, we must not even check the path.  */
		if (tstimes[0].tv_nsec == UTIME_OMIT &&
		    tstimes[1].tv_nsec == UTIME_OMIT)
			return 0;
	}

	return do_utimes(dfd, filename, utimes ? tstimes : NULL, flags);
}


