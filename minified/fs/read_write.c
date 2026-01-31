#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/splice.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include "internal.h"

#include <linux/uaccess.h>
#include <asm/unistd.h>

ssize_t kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
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

	/* .read callback removed - only read_iter used */
	if (unlikely(!file->f_op->read_iter)) {
		pr_warn_ratelimited(
			"kernel read not supported for file %pD4 (pid: %d comm: %.20s)\n",
			file, current->pid, current->comm);
		return -EINVAL;
	}

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = pos ? *pos : 0;
	iov_iter_kvec(&iter, READ, &iov, 1, iov.iov_len);
	ret = file->f_op->read_iter(&kiocb, &iter);
	if (ret > 0 && pos)
		*pos = kiocb.ki_pos;
	return ret;
}

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

	if (count > MAX_RW_COUNT)
		count = MAX_RW_COUNT;
	/* file_start_write inlined - single caller */
	if (S_ISREG(file_inode(file)->i_mode))
		sb_start_write(file_inode(file)->i_sb);
	/* .write callback removed - only write_iter used */
	if (file->f_op->write_iter) {
		struct iovec iov = { .iov_base = (void __user *)buf,
				     .iov_len = count };
		struct kiocb kiocb;
		struct iov_iter iter;
		init_sync_kiocb(&kiocb, file);
		kiocb.ki_pos = (pos ? *pos : 0);
		iov_iter_init(&iter, WRITE, &iov, 1, count);
		/* call_write_iter inlined */
		ret = file->f_op->write_iter(&kiocb, &iter);
		BUG_ON(ret == -EIOCBQUEUED);
		if (ret > 0 && pos)
			*pos = kiocb.ki_pos;
	} else
		ret = -EINVAL;
	/* file_end_write inlined - single caller */
	if (S_ISREG(file_inode(file)->i_mode))
		__sb_end_write(file_inode(file)->i_sb, SB_FREEZE_WRITE);
	return ret;
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
		/* fdput_pos inlined */
		if (f.flags & FDPUT_POS_UNLOCK)
			__f_unlock_pos(f.file);
		fdput(f);
	}

	return ret;
}

/* generic_write_checks removed - never called */
