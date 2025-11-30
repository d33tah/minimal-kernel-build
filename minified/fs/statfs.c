/* Stub: statfs syscalls not needed for minimal kernel */

#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/statfs.h>

/* Stub: vfs_get_fsid not used in minimal kernel */
int vfs_get_fsid(struct dentry *dentry, __kernel_fsid_t *fsid)
{
	return -ENOSYS;
}

/* Stub: vfs_statfs not used in minimal kernel */
int vfs_statfs(const struct path *path, struct kstatfs *buf)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(statfs, const char __user *, pathname, struct statfs __user *, buf)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(statfs64, const char __user *, pathname, size_t, sz, struct statfs64 __user *, buf)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(fstatfs, unsigned int, fd, struct statfs __user *, buf)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(fstatfs64, unsigned int, fd, size_t, sz, struct statfs64 __user *, buf)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(ustat, unsigned, dev, struct ustat __user *, ubuf)
{
	return -ENOSYS;
}
