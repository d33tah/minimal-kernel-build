#include <linux/module.h>
#include <linux/namei.h>

#include "internal.h"

int vfs_open(const struct path *path, struct file *file)
{
	static const struct file_operations empty_fops = {};
	struct inode *inode = d_inode(path->dentry);
	int error;
	int (*open)(struct inode *, struct file *) = NULL;

	file->f_path = *path;
	path_get(&file->f_path);
	file->f_inode = inode;
	file->f_mapping = inode->i_mapping;

	if (unlikely(file->f_flags & O_PATH)) {
		file->f_mode = FMODE_PATH | FMODE_OPENED;
		file->f_op = &empty_fops;
		return 0;
	}

	if (file->f_mode & FMODE_WRITE && !special_file(inode->i_mode)) {
		error = atomic_inc_unless_negative(&inode->i_writecount) ?
				0 :
				-ETXTBSY;
		if (unlikely(error))
			goto cleanup_file;
		error = __mnt_want_write(file->f_path.mnt);
		if (unlikely(error)) {
			put_write_access(inode);
			goto cleanup_file;
		}
		file->f_mode |= FMODE_WRITER;
	}

	if (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode))
		file->f_mode |= FMODE_ATOMIC_POS;

	file->f_op = fops_get(inode->i_fop);
	if (WARN_ON(!file->f_op)) {
		error = -ENODEV;
		goto cleanup_all;
	}

	file->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;
	if (!open)
		open = file->f_op->open;
	if (open) {
		error = open(inode, file);
		if (error)
			goto cleanup_all;
	}
	file->f_mode |= FMODE_OPENED;
	if ((file->f_mode & FMODE_READ) && likely(file->f_op->read_iter))
		file->f_mode |= FMODE_CAN_READ;
	if ((file->f_mode & FMODE_WRITE) && likely(file->f_op->write_iter))
		file->f_mode |= FMODE_CAN_WRITE;

	file->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	return 0;

cleanup_all:
	if (WARN_ON_ONCE(error > 0))
		error = -EINVAL;
	fops_put(file->f_op);
	if (file->f_mode & FMODE_WRITER) {
		put_write_access(inode);
		__mnt_drop_write(file->f_path.mnt);
	}
cleanup_file:
	path_put(&file->f_path);
	file->f_path.mnt = NULL;
	file->f_path.dentry = NULL;
	file->f_inode = NULL;
	return error;
}

#define WILL_CREATE(flags) (flags & (O_CREAT | __O_TMPFILE))
#define O_PATH_FLAGS (O_DIRECTORY | O_NOFOLLOW | O_PATH | O_CLOEXEC)

static inline struct open_how build_open_how(int flags, umode_t mode)
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

static inline int build_open_flags(const struct open_how *how,
				   struct open_flags *op)
{
	u64 flags = how->flags & ~(FMODE_NONOTIFY | O_CLOEXEC);
	op->open_flag = flags;
	op->mode = WILL_CREATE(flags) ? (how->mode | S_IFREG) : 0;
	op->acc_mode = ACC_MODE(flags);
	op->intent = (flags & O_PATH) ? 0 : LOOKUP_OPEN;
	op->lookup_flags = (flags & O_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	return 0;
}

struct file *filp_open(const char *filename, int flags, umode_t mode)
{
	struct open_flags op;
	struct open_how how = build_open_how(flags, mode);
	int err = build_open_flags(&how, &op);
	struct filename *name;
	struct file *file;

	if (err)
		return ERR_PTR(err);

	name = getname_kernel(filename);
	file = ERR_CAST(name);

	if (!IS_ERR(name)) {
		file = do_filp_open(AT_FDCWD, name, &op);
		putname(name);
	}
	return file;
}
