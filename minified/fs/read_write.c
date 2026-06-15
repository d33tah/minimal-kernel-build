
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include "internal.h"

#include <linux/uaccess.h>
#include <asm/unistd.h>

loff_t noop_llseek(struct file *file, loff_t offset, int whence)
{
	return file->f_pos;
}

loff_t no_llseek(struct file *file, loff_t offset, int whence)
{
	return -ESPIPE;
}

int rw_verify_area(int read_write, struct file *file, const loff_t *ppos, size_t count)
{
	 
	return 0;
}

static int warn_unsupported(struct file *file, const char *op)
{
	pr_warn_ratelimited(
		"kernel %s not supported for file %pD4 (pid: %d comm: %.20s)\n",
		op, file, current->pid, current->comm);
	return -EINVAL;
}

ssize_t __kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
	struct kvec iov = {
		.iov_base	= buf,
		.iov_len	= min_t(size_t, count, MAX_RW_COUNT),
	};
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	if (WARN_ON_ONCE(!(file->f_mode & FMODE_READ)))
		return -EINVAL;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;
	 
	if (unlikely(!file->f_op->read_iter))
		return warn_unsupported(file, "read");

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = pos ? *pos : 0;
	iov_iter_kvec(&iter, READ, &iov, 1, iov.iov_len);
	ret = file->f_op->read_iter(&kiocb, &iter);
	if (ret > 0) {
		if (pos)
			*pos = kiocb.ki_pos;
	}
	return ret;
}

ssize_t kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	ret = rw_verify_area(READ, file, pos, count);
	if (ret)
		return ret;
	return __kernel_read(file, buf, count, pos);
}

static ssize_t new_sync_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
	struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = (ppos ? *ppos : 0);
	iov_iter_init(&iter, WRITE, &iov, 1, len);

	ret = call_write_iter(filp, &kiocb, &iter);
	BUG_ON(ret == -EIOCBQUEUED);
	if (ret > 0 && ppos)
		*ppos = kiocb.ki_pos;
	return ret;
}

ssize_t __kernel_write(struct file *file, const void *buf, size_t count, loff_t *pos)
{
	struct kvec iov = {
		.iov_base	= (void *)buf,
		.iov_len	= min_t(size_t, count, MAX_RW_COUNT),
	};
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	if (WARN_ON_ONCE(!(file->f_mode & FMODE_WRITE)))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;
	 
	if (unlikely(!file->f_op->write_iter))
		return warn_unsupported(file, "write");

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = pos ? *pos : 0;
	iov_iter_kvec(&iter, WRITE, &iov, 1, iov.iov_len);
	ret = file->f_op->write_iter(&kiocb, &iter);
	if (ret > 0) {
		if (pos)
			*pos = kiocb.ki_pos;
	}
	return ret;
}

ssize_t kernel_write(struct file *file, const void *buf, size_t count,
			    loff_t *pos)
{
	ssize_t ret;

	ret = rw_verify_area(WRITE, file, pos, count);
	if (ret)
		return ret;

	file_start_write(file);
	ret =  __kernel_write(file, buf, count, pos);
	file_end_write(file);
	return ret;
}

ssize_t vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;
	if (unlikely(!access_ok(buf, count)))
		return -EFAULT;

	ret = rw_verify_area(WRITE, file, pos, count);
	if (ret)
		return ret;
	if (count > MAX_RW_COUNT)
		count =  MAX_RW_COUNT;
	file_start_write(file);
	/*
	 * No file_operations on this build sets the non-iter ->write op (ramfs
	 * and tty both expose only ->write_iter), so the dispatch always goes
	 * through new_sync_write().
	 */
	ret = new_sync_write(file, buf, count, pos);
	file_end_write(file);
	return ret;
}

static inline loff_t *file_ppos(struct file *file)
{
	return file->f_mode & FMODE_STREAM ? NULL : &file->f_pos;
}

SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf,
		size_t, count)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = file_ppos(f.file);
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_write(f.file, buf, count, ppos);
		if (ret >= 0 && ppos)
			f.file->f_pos = pos;
		fdput_pos(f);
	}

	return ret;
}

ssize_t generic_write_checks(struct kiocb *iocb, struct iov_iter *from)
{
	iov_iter_truncate(from, iov_iter_count(from));
	return iov_iter_count(from);
}

