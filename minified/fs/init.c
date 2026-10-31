#include <linux/namei.h>
#include <linux/file.h>

int __init init_mknod(const char *filename, umode_t mode, unsigned int dev)
{
	struct dentry *dentry;
	struct path path;
	int error;

	if (!S_ISCHR(mode))
		return -EINVAL;

	dentry = kern_path_create(filename, &path, 0);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	mode &= ~current_umask();
	error = vfs_mknod(mnt_user_ns(path.mnt), path.dentry->d_inode, dentry,
			  mode, new_decode_dev(dev));
	done_path_create(&path, dentry);
	return error;
}

int __init init_mkdir(const char *pathname, umode_t mode)
{
	struct dentry *dentry;
	struct path path;
	int error;

	dentry = kern_path_create(pathname, &path, LOOKUP_DIRECTORY);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	mode &= ~current_umask();
	error = vfs_mkdir(mnt_user_ns(path.mnt), path.dentry->d_inode, dentry,
			  mode);
	done_path_create(&path, dentry);
	return error;
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
