#include <linux/namei.h>
#include <linux/fs_struct.h>
#include <linux/file.h>

int __init init_chroot(const char *filename)
{
	struct path path;
	int error;

	error = kern_path(filename, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &path);
	if (error)
		return error;
	error = path_permission(&path, MAY_EXEC | MAY_CHDIR);
	if (error)
		goto dput_and_out;
	error = 0;
	set_fs_root(current->fs, &path);
dput_and_out:
	path_put(&path);
	return error;
}

int __init init_eaccess(const char *filename)
{
	struct path path;
	int error;

	error = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (error)
		return error;
	error = path_permission(&path, MAY_ACCESS);
	path_put(&path);
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
