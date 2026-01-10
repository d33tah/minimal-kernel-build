#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
/* fsnotify.h removed - unused */
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/splice.h>
/* compat.h removed - unused */
#include <linux/mount.h>
#include <linux/fs.h>
#include "internal.h"

#include <linux/uaccess.h>
#include <asm/unistd.h>

/* vfs_setpos, generic_file_llseek_size, generic_file_llseek, noop_llseek,
 * no_llseek, vfs_llseek removed - llseek callback removed from file_operations
 */

/* Stub: lseek syscalls not needed for Hello World */
SYSCALL_DEFINE3(lseek, unsigned int, fd, off_t, offset, unsigned int, whence)
{
	return -ENOSYS;
}

#if !defined(CONFIG_64BIT) || defined(CONFIG_COMPAT) || \
	defined(__ARCH_WANT_SYS_LLSEEK)
SYSCALL_DEFINE5(llseek, unsigned int, fd, unsigned long, offset_high,
		unsigned long, offset_low, loff_t __user *, result,
		unsigned int, whence)
{
	return -ENOSYS;
}
#endif

/* rw_verify_area removed - always returns 0, all callers already removed checks */

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
		.iov_base = buf,
		.iov_len = min_t(size_t, count, MAX_RW_COUNT),
	};
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	if (WARN_ON_ONCE(!(file->f_mode & FMODE_READ)))
		return -EINVAL;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;

	if (unlikely(!file->f_op->read_iter || file->f_op->read))
		return warn_unsupported(file, "read");

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = pos ? *pos : 0;
	iov_iter_kvec(&iter, READ, &iov, 1, iov.iov_len);
	ret = file->f_op->read_iter(&kiocb, &iter);
	if (ret > 0 && pos)
		*pos = kiocb.ki_pos;
	return ret;
}

ssize_t kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
	/* rw_verify_area always returns 0 - check removed */
	return __kernel_read(file, buf, count, pos);
}

/* kernel_write and __kernel_write removed - never called */

ssize_t vfs_write(struct file *file, const char __user *buf, size_t count,
		  loff_t *pos)
{
	ssize_t ret;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;
	if (unlikely(!access_ok(buf, count)))
		return -EFAULT;

	/* rw_verify_area always returns 0 - check removed */
	if (count > MAX_RW_COUNT)
		count = MAX_RW_COUNT;
	file_start_write(file);
	if (file->f_op->write)
		ret = file->f_op->write(file, buf, count, pos);
	else if (file->f_op->write_iter) {
		/* Inlined new_sync_write */
		struct iovec iov = { .iov_base = (void __user *)buf,
				     .iov_len = count };
		struct kiocb kiocb;
		struct iov_iter iter;
		init_sync_kiocb(&kiocb, file);
		kiocb.ki_pos = (pos ? *pos : 0);
		iov_iter_init(&iter, WRITE, &iov, 1, count);
		ret = call_write_iter(file, &kiocb, &iter);
		BUG_ON(ret == -EIOCBQUEUED);
		if (ret > 0 && pos)
			*pos = kiocb.ki_pos;
	} else
		ret = -EINVAL;
	file_end_write(file);
	return ret;
}

/* Stub: read syscall */
SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf, size_t,
		count)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = f.file->f_mode & FMODE_STREAM ?
					    NULL :
					    &f.file->f_pos;
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

SYSCALL_DEFINE4(pread64, unsigned int, fd, char __user *, buf, size_t, count,
		loff_t, pos)
{
	return -ENOSYS;
}

SYSCALL_DEFINE4(pwrite64, unsigned int, fd, const char __user *, buf, size_t,
		count, loff_t, pos)
{
	return -ENOSYS;
}

/* do_readv, do_writev removed - unused stubs */
SYSCALL_DEFINE3(readv, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(writev, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen)
{
	return -ENOSYS;
}

SYSCALL_DEFINE5(preadv, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h)
{
	return -ENOSYS; /* Stub: preadv not used by minimal kernel */
}

SYSCALL_DEFINE6(preadv2, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h,
		rwf_t, flags)
{
	/* Stub: preadv2 not needed for minimal kernel, preadv suffices */
	return -ENOSYS;
}

SYSCALL_DEFINE5(pwritev, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h)
{
	return -ENOSYS; /* Stub: pwritev not used by minimal kernel */
}

SYSCALL_DEFINE6(pwritev2, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h,
		rwf_t, flags)
{
	/* Stub: pwritev2 not needed for minimal kernel, pwritev suffices */
	return -ENOSYS;
}

SYSCALL_DEFINE4(sendfile, int, out_fd, int, in_fd, off_t __user *, offset,
		size_t, count)
{
	return -ENOSYS;
}

SYSCALL_DEFINE4(sendfile64, int, out_fd, int, in_fd, loff_t __user *, offset,
		size_t, count)
{
	return -ENOSYS;
}

SYSCALL_DEFINE6(copy_file_range, int, fd_in, loff_t __user *, off_in, int,
		fd_out, loff_t __user *, off_out, size_t, len, unsigned int,
		flags)
{
	return -ENOSYS;
}

ssize_t generic_write_checks(struct kiocb *iocb, struct iov_iter *from)
{
	iov_iter_truncate(from, iov_iter_count(from));
	return iov_iter_count(from);
}
