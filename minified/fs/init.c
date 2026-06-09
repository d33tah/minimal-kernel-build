#include <linux/init.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/file.h>
#include <linux/init_syscalls.h>
#include <linux/security.h>
#include "internal.h"


int __init init_chown(const char *filename, uid_t user, gid_t group, int flags)
{
	int lookup_flags = (flags & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	struct path path;
	int error;

	error = kern_path(filename, lookup_flags, &path);
	if (error)
		return error;
	error = mnt_want_write(path.mnt);
	if (!error) {
		error = chown_common(&path, user, group);
		mnt_drop_write(path.mnt);
	}
	path_put(&path);
	return error;
}

int __init init_chmod(const char *filename, umode_t mode)
{
	struct path path;
	int error;

	error = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (error)
		return error;
	error = chmod_common(&path, mode);
	path_put(&path);
	return error;
}

int __init init_stat(const char *filename, struct kstat *stat, int flags)
{
	int lookup_flags = (flags & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	struct path path;
	int error;

	error = kern_path(filename, lookup_flags, &path);
	if (error)
		return error;
	error = vfs_getattr(&path, stat, STATX_BASIC_STATS,
			    flags | AT_NO_AUTOMOUNT);
	path_put(&path);
	return error;
}

int __init init_mknod(const char *filename, umode_t mode, unsigned int dev)
{
	struct dentry *dentry;
	struct path path;
	int error;

	if (S_ISFIFO(mode) || S_ISSOCK(mode))
		dev = 0;
	else if (!(S_ISBLK(mode) || S_ISCHR(mode)))
		return -EINVAL;

	dentry = kern_path_create(AT_FDCWD, filename, &path, 0);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	if (!IS_POSIXACL(path.dentry->d_inode))
		mode &= ~current_umask();
	error = vfs_mknod(mnt_user_ns(path.mnt), path.dentry->d_inode,
			  dentry, mode, new_decode_dev(dev));
	done_path_create(&path, dentry);
	return error;
}

int __init init_link(const char *oldname, const char *newname)
{
	struct dentry *new_dentry;
	struct path old_path, new_path;
	struct user_namespace *mnt_userns;
	int error;

	error = kern_path(oldname, 0, &old_path);
	if (error)
		return error;

	new_dentry = kern_path_create(AT_FDCWD, newname, &new_path, 0);
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto out;

	error = -EXDEV;
	if (old_path.mnt != new_path.mnt)
		goto out_dput;
	mnt_userns = mnt_user_ns(new_path.mnt);
	error = may_linkat(mnt_userns, &old_path);
	if (unlikely(error))
		goto out_dput;
	error = vfs_link(old_path.dentry, mnt_userns, new_path.dentry->d_inode,
			 new_dentry, NULL);
out_dput:
	done_path_create(&new_path, new_dentry);
out:
	path_put(&old_path);
	return error;
}

int __init init_symlink(const char *oldname, const char *newname)
{
	struct dentry *dentry;
	struct path path;
	int error;

	dentry = kern_path_create(AT_FDCWD, newname, &path, 0);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	error = vfs_symlink(mnt_user_ns(path.mnt), path.dentry->d_inode,
			    dentry, oldname);
	done_path_create(&path, dentry);
	return error;
}

int __init init_unlink(const char *pathname)
{
	return do_unlinkat(AT_FDCWD, getname_kernel(pathname));
}

int __init init_mkdir(const char *pathname, umode_t mode)
{
	struct dentry *dentry;
	struct path path;
	int error;

	dentry = kern_path_create(AT_FDCWD, pathname, &path, LOOKUP_DIRECTORY);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	if (!IS_POSIXACL(path.dentry->d_inode))
		mode &= ~current_umask();
	error = vfs_mkdir(mnt_user_ns(path.mnt), path.dentry->d_inode,
			  dentry, mode);
	done_path_create(&path, dentry);
	return error;
}

int __init init_rmdir(const char *pathname)
{
	return do_rmdir(AT_FDCWD, getname_kernel(pathname));
}

int __init init_dup(struct file *file)
{
	int fd;

	fd = get_unused_fd_flags(0);
	if (fd < 0)
		return fd;
	fd_install(fd, get_file(file));
	return 0;
}
