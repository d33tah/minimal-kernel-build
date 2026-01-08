
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/init_task.h> /* for securebits defines */
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/rcupdate.h>
/* audit.h removed - unused */
/* FALLOC_FL_*, FS_IOC_*, space_resv removed - unused */

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
	if (filp) {
		newattrs.ia_file = filp;
		newattrs.ia_valid |= ATTR_FILE;
	}

	/* dentry_needs_remove_privs always returns 0 - dead code removed */

	inode_lock(dentry->d_inode);

	ret = notify_change(mnt_userns, dentry, &newattrs, NULL);
	inode_unlock(dentry->d_inode);
	return ret;
}

/* vfs_truncate removed - never called */

/* Stub: truncate not needed for Hello World kernel */
SYSCALL_DEFINE2(truncate, const char __user *, path, long, length)
{
	return -ENOSYS;
}

/* Stub: ftruncate not needed for Hello World kernel */
SYSCALL_DEFINE2(ftruncate, unsigned int, fd, unsigned long, length)
{
	return -ENOSYS;
}

#if BITS_PER_LONG == 32
SYSCALL_DEFINE2(truncate64, const char __user *, path, loff_t, length)
{
	return -ENOSYS;
}
SYSCALL_DEFINE2(ftruncate64, unsigned int, fd, loff_t, length)
{
	return -ENOSYS;
}
#endif

/* Stub: fallocate not needed for Hello World */
SYSCALL_DEFINE4(fallocate, int, fd, int, mode, loff_t, offset, loff_t, len)
{
	return -ENOSYS;
}

/* Stub: access syscalls not needed for Hello World */
SYSCALL_DEFINE3(faccessat, int, dfd, const char __user *, filename, int, mode)
{
	return -ENOSYS;
}

SYSCALL_DEFINE4(faccessat2, int, dfd, const char __user *, filename, int, mode,
		int, flags)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(access, const char __user *, filename, int, mode)
{
	return -ENOSYS;
}

SYSCALL_DEFINE1(chdir, const char __user *, filename)
{
	return -ENOSYS;
}

SYSCALL_DEFINE1(fchdir, unsigned int, fd)
{
	return -ENOSYS;
}

SYSCALL_DEFINE1(chroot, const char __user *, filename)
{
	/* Stubbed: chroot not needed for minimal kernel */
	return -ENOSYS;
}

int chmod_common(const struct path *path, umode_t mode)
{
	/* Stub: chmod not needed for minimal kernel */
	return -EOPNOTSUPP;
}

int vfs_fchmod(struct file *file, umode_t mode)
{
	/* audit_file - empty stub */
	return chmod_common(&file->f_path, mode);
}

/* Stub: fchmod not needed for Hello World */
SYSCALL_DEFINE2(fchmod, unsigned int, fd, umode_t, mode)
{
	return -ENOSYS;
}

/* Stub: chmod syscalls not needed for Hello World */
SYSCALL_DEFINE3(fchmodat, int, dfd, const char __user *, filename, umode_t,
		mode)
{
	return -ENOSYS;
}

SYSCALL_DEFINE2(chmod, const char __user *, filename, umode_t, mode)
{
	return -ENOSYS;
}

/* chown_common removed - only caller was init_chown which was removed */

/* Stub: chown syscalls not needed for Hello World */
SYSCALL_DEFINE5(fchownat, int, dfd, const char __user *, filename, uid_t, user,
		gid_t, group, int, flag)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(chown, const char __user *, filename, uid_t, user, gid_t, group)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(lchown, const char __user *, filename, uid_t, user, gid_t,
		group)
{
	return -ENOSYS;
}

int vfs_fchown(struct file *file, uid_t user, gid_t group)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(fchown, unsigned int, fd, uid_t, user, gid_t, group)
{
	return -ENOSYS; /* stubbed */
}

static int do_dentry_open(struct file *f, struct inode *inode,
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

	/* security_file_open always returns 0 - dead code removed */
	/* break_lease always returns 0 - dead code removed */
	f->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;
	if (!open)
		open = f->f_op->open;
	if (open) {
		error = open(inode, f);
		if (error)
			goto cleanup_all;
	}
	f->f_mode |= FMODE_OPENED;
	if ((f->f_mode & FMODE_READ) &&
	    likely(f->f_op->read || f->f_op->read_iter))
		f->f_mode |= FMODE_CAN_READ;
	if ((f->f_mode & FMODE_WRITE) &&
	    likely(f->f_op->write || f->f_op->write_iter))
		f->f_mode |= FMODE_CAN_WRITE;
	if (f->f_mapping->a_ops && f->f_mapping->a_ops->direct_IO)
		f->f_mode |= FMODE_CAN_ODIRECT;

	f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	/* file_ra_state_init removed - was empty stub */

	if ((f->f_flags & O_DIRECT) && !(f->f_mode & FMODE_CAN_ODIRECT))
		return -EINVAL;

	/* filemap_nr_thps always returns 0 - THP handling removed */

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

/* finish_open removed - never called */

int vfs_open(const struct path *path, struct file *file)
{
	file->f_path = *path;
	return do_dentry_open(file, d_backing_inode(path->dentry), NULL);
}

#define WILL_CREATE(flags) (flags & (O_CREAT | __O_TMPFILE))
#define O_PATH_FLAGS (O_DIRECTORY | O_NOFOLLOW | O_PATH | O_CLOEXEC)

inline struct open_how build_open_how(int flags, umode_t mode)
{
	struct open_how how = {
		.flags = flags & VALID_OPEN_FLAGS,
		.mode = mode & S_IALLUGO,
	};

	if (how.flags & O_PATH)
		how.flags &= O_PATH_FLAGS;

	if (!WILL_CREATE(how.flags))
		how.mode = 0;
	return how;
}

/* Stub: build_open_flags simplified for Hello World */
inline int build_open_flags(const struct open_how *how, struct open_flags *op)
{
	u64 flags = how->flags & ~(FMODE_NONOTIFY | O_CLOEXEC);
	op->open_flag = flags;
	op->mode = WILL_CREATE(flags) ? (how->mode | S_IFREG) : 0;
	op->acc_mode = ACC_MODE(flags);
	op->intent = (flags & O_PATH) ? 0 : LOOKUP_OPEN;
	op->lookup_flags = (flags & O_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	return 0;
}

struct file *file_open_name(struct filename *name, int flags, umode_t mode)
{
	struct open_flags op;
	struct open_how how = build_open_how(flags, mode);
	int err = build_open_flags(&how, &op);
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

/* Stub: open syscalls */
SYSCALL_DEFINE3(open, const char __user *, filename, int, flags, umode_t, mode)
{
	return -ENOSYS;
}
SYSCALL_DEFINE4(openat, int, dfd, const char __user *, filename, int, flags,
		umode_t, mode)
{
	return -ENOSYS;
}
SYSCALL_DEFINE4(openat2, int, dfd, const char __user *, filename,
		struct open_how __user *, how, size_t, usize)
{
	return -ENOSYS;
}

/* Stub: creat not needed for Hello World */
SYSCALL_DEFINE2(creat, const char __user *, pathname, umode_t, mode)
{
	return -ENOSYS;
}

int filp_close(struct file *filp, fl_owner_t id)
{
	int retval = 0;

	if (!file_count(filp)) {
		printk(KERN_ERR "VFS: Close: file count is 0\n");
		return 0;
	}

	if (filp->f_op->flush)
		retval = filp->f_op->flush(filp, id);

	fput(filp);
	return retval;
}

/* Stub: close syscall */
SYSCALL_DEFINE1(close, unsigned int, fd)
{
	return -ENOSYS;
}

/* Stub: close_range not needed for Hello World */
SYSCALL_DEFINE3(close_range, unsigned int, fd, unsigned int, max_fd,
		unsigned int, flags)
{
	return -ENOSYS;
}

SYSCALL_DEFINE0(vhangup)
{
	/* Stub: vhangup not needed for minimal kernel */
	return -EPERM;
}

int nonseekable_open(struct inode *inode, struct file *filp)
{
	filp->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	return 0;
}

/* Removed: stream_open - never called */
