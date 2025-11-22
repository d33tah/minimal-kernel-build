 
 

#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/sched/xacct.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/fsnotify.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/splice.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include "internal.h"

#include <linux/uaccess.h>
#include <asm/unistd.h>

const struct file_operations generic_ro_fops = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.mmap		= generic_file_readonly_mmap,
	.splice_read	= generic_file_splice_read,
};


static inline bool unsigned_offsets(struct file *file)
{
	return file->f_mode & FMODE_UNSIGNED_OFFSET;
}

 
loff_t vfs_setpos(struct file *file, loff_t offset, loff_t maxsize)
{
	if (offset < 0 && !unsigned_offsets(file))
		return -EINVAL;
	if (offset > maxsize)
		return -EINVAL;

	if (offset != file->f_pos) {
		file->f_pos = offset;
		file->f_version = 0;
	}
	return offset;
}

 
loff_t
generic_file_llseek_size(struct file *file, loff_t offset, int whence,
		loff_t maxsize, loff_t eof)
{
	switch (whence) {
	case SEEK_END:
		offset += eof;
		break;
	case SEEK_CUR:
		 
		if (offset == 0)
			return file->f_pos;
		 
		spin_lock(&file->f_lock);
		offset = vfs_setpos(file, file->f_pos + offset, maxsize);
		spin_unlock(&file->f_lock);
		return offset;
	case SEEK_DATA:
		 
		if ((unsigned long long)offset >= eof)
			return -ENXIO;
		break;
	case SEEK_HOLE:
		 
		if ((unsigned long long)offset >= eof)
			return -ENXIO;
		offset = eof;
		break;
	}

	return vfs_setpos(file, offset, maxsize);
}

 
loff_t generic_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;

	return generic_file_llseek_size(file, offset, whence,
					inode->i_sb->s_maxbytes,
					i_size_read(inode));
}

 
loff_t fixed_size_llseek(struct file *file, loff_t offset, int whence, loff_t size)
{
	switch (whence) {
	case SEEK_SET: case SEEK_CUR: case SEEK_END:
		return generic_file_llseek_size(file, offset, whence,
						size, size);
	default:
		return -EINVAL;
	}
}

 
loff_t no_seek_end_llseek(struct file *file, loff_t offset, int whence)
{
	switch (whence) {
	case SEEK_SET: case SEEK_CUR:
		return generic_file_llseek_size(file, offset, whence,
						OFFSET_MAX, 0);
	default:
		return -EINVAL;
	}
}

 
loff_t no_seek_end_llseek_size(struct file *file, loff_t offset, int whence, loff_t size)
{
	switch (whence) {
	case SEEK_SET: case SEEK_CUR:
		return generic_file_llseek_size(file, offset, whence,
						size, 0);
	default:
		return -EINVAL;
	}
}

 
loff_t noop_llseek(struct file *file, loff_t offset, int whence)
{
	return file->f_pos;
}

loff_t no_llseek(struct file *file, loff_t offset, int whence)
{
	return -ESPIPE;
}

loff_t default_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file_inode(file);
	loff_t retval;

	inode_lock(inode);
	switch (whence) {
		case SEEK_END:
			offset += i_size_read(inode);
			break;
		case SEEK_CUR:
			if (offset == 0) {
				retval = file->f_pos;
				goto out;
			}
			offset += file->f_pos;
			break;
		case SEEK_DATA:
			 
			if (offset >= inode->i_size) {
				retval = -ENXIO;
				goto out;
			}
			break;
		case SEEK_HOLE:
			 
			if (offset >= inode->i_size) {
				retval = -ENXIO;
				goto out;
			}
			offset = inode->i_size;
			break;
	}
	retval = -EINVAL;
	if (offset >= 0 || unsigned_offsets(file)) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = 0;
		}
		retval = offset;
	}
out:
	inode_unlock(inode);
	return retval;
}

loff_t vfs_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t (*fn)(struct file *, loff_t, int);

	fn = no_llseek;
	if (file->f_mode & FMODE_LSEEK) {
		if (file->f_op->llseek)
			fn = file->f_op->llseek;
	}
	return fn(file, offset, whence);
}

static off_t ksys_lseek(unsigned int fd, off_t offset, unsigned int whence)
{
	off_t retval;
	struct fd f = fdget_pos(fd);
	if (!f.file)
		return -EBADF;

	retval = -EINVAL;
	if (whence <= SEEK_MAX) {
		loff_t res = vfs_llseek(f.file, offset, whence);
		retval = res;
		if (res != (loff_t)retval)
			retval = -EOVERFLOW;	 
	}
	fdput_pos(f);
	return retval;
}

SYSCALL_DEFINE3(lseek, unsigned int, fd, off_t, offset, unsigned int, whence)
{
	return ksys_lseek(fd, offset, whence);
}


#if !defined(CONFIG_64BIT) || defined(CONFIG_COMPAT) || \
	defined(__ARCH_WANT_SYS_LLSEEK)
SYSCALL_DEFINE5(llseek, unsigned int, fd, unsigned long, offset_high,
		unsigned long, offset_low, loff_t __user *, result,
		unsigned int, whence)
{
	int retval;
	struct fd f = fdget_pos(fd);
	loff_t offset;

	if (!f.file)
		return -EBADF;

	retval = -EINVAL;
	if (whence > SEEK_MAX)
		goto out_putf;

	offset = vfs_llseek(f.file, ((loff_t) offset_high << 32) | offset_low,
			whence);

	retval = (int)offset;
	if (offset >= 0) {
		retval = -EFAULT;
		if (!copy_to_user(result, &offset, sizeof(offset)))
			retval = 0;
	}
out_putf:
	fdput_pos(f);
	return retval;
}
#endif

int rw_verify_area(int read_write, struct file *file, const loff_t *ppos, size_t count)
{
	 
	return 0;
}

static ssize_t new_sync_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	struct iovec iov = { .iov_base = buf, .iov_len = len };
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = (ppos ? *ppos : 0);
	iov_iter_init(&iter, READ, &iov, 1, len);

	ret = call_read_iter(filp, &kiocb, &iter);
	BUG_ON(ret == -EIOCBQUEUED);
	if (ppos)
		*ppos = kiocb.ki_pos;
	return ret;
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
	 
	if (unlikely(!file->f_op->read_iter || file->f_op->read))
		return warn_unsupported(file, "read");

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = pos ? *pos : 0;
	iov_iter_kvec(&iter, READ, &iov, 1, iov.iov_len);
	ret = file->f_op->read_iter(&kiocb, &iter);
	if (ret > 0) {
		if (pos)
			*pos = kiocb.ki_pos;
		fsnotify_access(file);
		add_rchar(current, ret);
	}
	inc_syscr(current);
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

ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;
	if (unlikely(!access_ok(buf, count)))
		return -EFAULT;

	ret = rw_verify_area(READ, file, pos, count);
	if (ret)
		return ret;
	if (count > MAX_RW_COUNT)
		count =  MAX_RW_COUNT;

	if (file->f_op->read)
		ret = file->f_op->read(file, buf, count, pos);
	else if (file->f_op->read_iter)
		ret = new_sync_read(file, buf, count, pos);
	else
		ret = -EINVAL;
	if (ret > 0) {
		fsnotify_access(file);
		add_rchar(current, ret);
	}
	inc_syscr(current);
	return ret;
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
	 
	if (unlikely(!file->f_op->write_iter || file->f_op->write))
		return warn_unsupported(file, "write");

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = pos ? *pos : 0;
	iov_iter_kvec(&iter, WRITE, &iov, 1, iov.iov_len);
	ret = file->f_op->write_iter(&kiocb, &iter);
	if (ret > 0) {
		if (pos)
			*pos = kiocb.ki_pos;
		fsnotify_modify(file);
		add_wchar(current, ret);
	}
	inc_syscw(current);
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
	if (file->f_op->write)
		ret = file->f_op->write(file, buf, count, pos);
	else if (file->f_op->write_iter)
		ret = new_sync_write(file, buf, count, pos);
	else
		ret = -EINVAL;
	if (ret > 0) {
		fsnotify_modify(file);
		add_wchar(current, ret);
	}
	inc_syscw(current);
	file_end_write(file);
	return ret;
}

 
static inline loff_t *file_ppos(struct file *file)
{
	return file->f_mode & FMODE_STREAM ? NULL : &file->f_pos;
}

ssize_t ksys_read(unsigned int fd, char __user *buf, size_t count)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = file_ppos(f.file);
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_read(f.file, buf, count, ppos);
		if (ret >= 0 && ppos)
			f.file->f_pos = pos;
		fdput_pos(f);
	}
	return ret;
}

SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
{
	return ksys_read(fd, buf, count);
}

ssize_t ksys_write(unsigned int fd, const char __user *buf, size_t count)
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

SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf,
		size_t, count)
{
	return ksys_write(fd, buf, count);
}

ssize_t ksys_pread64(unsigned int fd, char __user *buf, size_t count,
		     loff_t pos)
{
	return -ENOSYS;
}

SYSCALL_DEFINE4(pread64, unsigned int, fd, char __user *, buf,
			size_t, count, loff_t, pos)
{
	return ksys_pread64(fd, buf, count, pos);
}


ssize_t ksys_pwrite64(unsigned int fd, const char __user *buf,
		      size_t count, loff_t pos)
{
	return -ENOSYS;
}

SYSCALL_DEFINE4(pwrite64, unsigned int, fd, const char __user *, buf,
			 size_t, count, loff_t, pos)
{
	return ksys_pwrite64(fd, buf, count, pos);
}


static ssize_t do_iter_readv_writev(struct file *filp, struct iov_iter *iter,
		loff_t *ppos, int type, rwf_t flags)
{
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	ret = kiocb_set_rw_flags(&kiocb, flags);
	if (ret)
		return ret;
	kiocb.ki_pos = (ppos ? *ppos : 0);

	if (type == READ)
		ret = call_read_iter(filp, &kiocb, iter);
	else
		ret = call_write_iter(filp, &kiocb, iter);
	BUG_ON(ret == -EIOCBQUEUED);
	if (ppos)
		*ppos = kiocb.ki_pos;
	return ret;
}

 
static ssize_t do_loop_readv_writev(struct file *filp, struct iov_iter *iter,
		loff_t *ppos, int type, rwf_t flags)
{
	ssize_t ret = 0;

	if (flags & ~RWF_HIPRI)
		return -EOPNOTSUPP;

	while (iov_iter_count(iter)) {
		struct iovec iovec = iov_iter_iovec(iter);
		ssize_t nr;

		if (type == READ) {
			nr = filp->f_op->read(filp, iovec.iov_base,
					      iovec.iov_len, ppos);
		} else {
			nr = filp->f_op->write(filp, iovec.iov_base,
					       iovec.iov_len, ppos);
		}

		if (nr < 0) {
			if (!ret)
				ret = nr;
			break;
		}
		ret += nr;
		if (nr != iovec.iov_len)
			break;
		iov_iter_advance(iter, nr);
	}

	return ret;
}

static ssize_t do_iter_read(struct file *file, struct iov_iter *iter,
		loff_t *pos, rwf_t flags)
{
	size_t tot_len;
	ssize_t ret = 0;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		goto out;
	ret = rw_verify_area(READ, file, pos, tot_len);
	if (ret < 0)
		return ret;

	if (file->f_op->read_iter)
		ret = do_iter_readv_writev(file, iter, pos, READ, flags);
	else
		ret = do_loop_readv_writev(file, iter, pos, READ, flags);
out:
	if (ret >= 0)
		fsnotify_access(file);
	return ret;
}

ssize_t vfs_iocb_iter_read(struct file *file, struct kiocb *iocb,
			   struct iov_iter *iter)
{
	size_t tot_len;
	ssize_t ret = 0;

	if (!file->f_op->read_iter)
		return -EINVAL;
	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		goto out;
	ret = rw_verify_area(READ, file, &iocb->ki_pos, tot_len);
	if (ret < 0)
		return ret;

	ret = call_read_iter(file, iocb, iter);
out:
	if (ret >= 0)
		fsnotify_access(file);
	return ret;
}

ssize_t vfs_iter_read(struct file *file, struct iov_iter *iter, loff_t *ppos,
		rwf_t flags)
{
	if (!file->f_op->read_iter)
		return -EINVAL;
	return do_iter_read(file, iter, ppos, flags);
}

static ssize_t do_iter_write(struct file *file, struct iov_iter *iter,
		loff_t *pos, rwf_t flags)
{
	size_t tot_len;
	ssize_t ret = 0;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		return 0;
	ret = rw_verify_area(WRITE, file, pos, tot_len);
	if (ret < 0)
		return ret;

	if (file->f_op->write_iter)
		ret = do_iter_readv_writev(file, iter, pos, WRITE, flags);
	else
		ret = do_loop_readv_writev(file, iter, pos, WRITE, flags);
	if (ret > 0)
		fsnotify_modify(file);
	return ret;
}

ssize_t vfs_iocb_iter_write(struct file *file, struct kiocb *iocb,
			    struct iov_iter *iter)
{
	size_t tot_len;
	ssize_t ret = 0;

	if (!file->f_op->write_iter)
		return -EINVAL;
	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		return 0;
	ret = rw_verify_area(WRITE, file, &iocb->ki_pos, tot_len);
	if (ret < 0)
		return ret;

	ret = call_write_iter(file, iocb, iter);
	if (ret > 0)
		fsnotify_modify(file);

	return ret;
}

ssize_t vfs_iter_write(struct file *file, struct iov_iter *iter, loff_t *ppos,
		rwf_t flags)
{
	if (!file->f_op->write_iter)
		return -EINVAL;
	return do_iter_write(file, iter, ppos, flags);
}

static ssize_t do_readv(unsigned long fd, const struct iovec __user *vec,
			unsigned long vlen, rwf_t flags)
{
	return -ENOSYS;
}

static ssize_t do_writev(unsigned long fd, const struct iovec __user *vec,
			 unsigned long vlen, rwf_t flags)
{
	return -ENOSYS;
}

static inline loff_t pos_from_hilo(unsigned long high, unsigned long low)
{
#define HALF_LONG_BITS (BITS_PER_LONG / 2)
	return (((loff_t)high << HALF_LONG_BITS) << HALF_LONG_BITS) | low;
}

static ssize_t do_preadv(unsigned long fd, const struct iovec __user *vec,
			 unsigned long vlen, loff_t pos, rwf_t flags)
{
	return -ENOSYS;
}

static ssize_t do_pwritev(unsigned long fd, const struct iovec __user *vec,
			  unsigned long vlen, loff_t pos, rwf_t flags)
{
	return -ENOSYS;
}

SYSCALL_DEFINE3(readv, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen)
{
	return do_readv(fd, vec, vlen, 0);
}

SYSCALL_DEFINE3(writev, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen)
{
	return do_writev(fd, vec, vlen, 0);
}

SYSCALL_DEFINE5(preadv, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	return do_preadv(fd, vec, vlen, pos, 0);
}

SYSCALL_DEFINE6(preadv2, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h,
		rwf_t, flags)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	if (pos == -1)
		return do_readv(fd, vec, vlen, flags);

	return do_preadv(fd, vec, vlen, pos, flags);
}

SYSCALL_DEFINE5(pwritev, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	return do_pwritev(fd, vec, vlen, pos, 0);
}

SYSCALL_DEFINE6(pwritev2, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h,
		rwf_t, flags)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	if (pos == -1)
		return do_writev(fd, vec, vlen, flags);

	return do_pwritev(fd, vec, vlen, pos, flags);
}

 

static ssize_t do_sendfile(int out_fd, int in_fd, loff_t *ppos,
		  	   size_t count, loff_t max)
{
	return -ENOSYS;
}

SYSCALL_DEFINE4(sendfile, int, out_fd, int, in_fd, off_t __user *, offset, size_t, count)
{
	return -ENOSYS;
}

SYSCALL_DEFINE4(sendfile64, int, out_fd, int, in_fd, loff_t __user *, offset, size_t, count)
{
	return -ENOSYS;
}


 

ssize_t generic_copy_file_range(struct file *file_in, loff_t pos_in,
				struct file *file_out, loff_t pos_out,
				size_t len, unsigned int flags)
{
	return do_splice_direct(file_in, &pos_in, file_out, &pos_out,
				len > MAX_RW_COUNT ? MAX_RW_COUNT : len, 0);
}

 
static int generic_copy_file_checks(struct file *file_in, loff_t pos_in,
				    struct file *file_out, loff_t pos_out,
				    size_t *req_count, unsigned int flags)
{
	struct inode *inode_in = file_inode(file_in);
	struct inode *inode_out = file_inode(file_out);
	uint64_t count = *req_count;
	loff_t size_in;
	int ret;

	ret = generic_file_rw_checks(file_in, file_out);
	if (ret)
		return ret;

	 
	if (file_out->f_op->copy_file_range) {
		if (file_in->f_op->copy_file_range !=
		    file_out->f_op->copy_file_range)
			return -EXDEV;
	} else if (file_inode(file_in)->i_sb != file_inode(file_out)->i_sb) {
		return -EXDEV;
	}

	 
	if (IS_IMMUTABLE(inode_out))
		return -EPERM;

	if (IS_SWAPFILE(inode_in) || IS_SWAPFILE(inode_out))
		return -ETXTBSY;

	 
	if (pos_in + count < pos_in || pos_out + count < pos_out)
		return -EOVERFLOW;

	 
	size_in = i_size_read(inode_in);
	if (pos_in >= size_in)
		count = 0;
	else
		count = min(count, size_in - (uint64_t)pos_in);

	ret = generic_write_check_limits(file_out, pos_out, &count);
	if (ret)
		return ret;

	 
	if (inode_in == inode_out &&
	    pos_out + count > pos_in &&
	    pos_out < pos_in + count)
		return -EINVAL;

	*req_count = count;
	return 0;
}

 
ssize_t vfs_copy_file_range(struct file *file_in, loff_t pos_in,
			    struct file *file_out, loff_t pos_out,
			    size_t len, unsigned int flags)
{
	ssize_t ret;

	if (flags != 0)
		return -EINVAL;

	ret = generic_copy_file_checks(file_in, pos_in, file_out, pos_out, &len,
				       flags);
	if (unlikely(ret))
		return ret;

	ret = rw_verify_area(READ, file_in, &pos_in, len);
	if (unlikely(ret))
		return ret;

	ret = rw_verify_area(WRITE, file_out, &pos_out, len);
	if (unlikely(ret))
		return ret;

	if (len == 0)
		return 0;

	file_start_write(file_out);

	 
	if (file_out->f_op->copy_file_range) {
		ret = file_out->f_op->copy_file_range(file_in, pos_in,
						      file_out, pos_out,
						      len, flags);
		goto done;
	}

	if (file_in->f_op->remap_file_range &&
	    file_inode(file_in)->i_sb == file_inode(file_out)->i_sb) {
		ret = file_in->f_op->remap_file_range(file_in, pos_in,
				file_out, pos_out,
				min_t(loff_t, MAX_RW_COUNT, len),
				REMAP_FILE_CAN_SHORTEN);
		if (ret > 0)
			goto done;
	}

	 
	ret = generic_copy_file_range(file_in, pos_in, file_out, pos_out, len,
				      flags);

done:
	if (ret > 0) {
		fsnotify_access(file_in);
		add_rchar(current, ret);
		fsnotify_modify(file_out);
		add_wchar(current, ret);
	}

	inc_syscr(current);
	inc_syscw(current);

	file_end_write(file_out);

	return ret;
}

SYSCALL_DEFINE6(copy_file_range, int, fd_in, loff_t __user *, off_in,
		int, fd_out, loff_t __user *, off_out,
		size_t, len, unsigned int, flags)
{
	/* Stubbed: copy_file_range not needed for minimal kernel */
	return -ENOSYS;
}

 
int generic_write_check_limits(struct file *file, loff_t pos, loff_t *count)
{
	struct inode *inode = file->f_mapping->host;
	loff_t max_size = inode->i_sb->s_maxbytes;
	loff_t limit = rlimit(RLIMIT_FSIZE);

	if (limit != RLIM_INFINITY) {
		if (pos >= limit) {
			send_sig(SIGXFSZ, current, 0);
			return -EFBIG;
		}
		*count = min(*count, limit - pos);
	}

	if (!(file->f_flags & O_LARGEFILE))
		max_size = MAX_NON_LFS;

	if (unlikely(pos >= max_size))
		return -EFBIG;

	*count = min(*count, max_size - pos);

	return 0;
}

 
int generic_write_checks_count(struct kiocb *iocb, loff_t *count)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;

	if (IS_SWAPFILE(inode))
		return -ETXTBSY;

	if (!*count)
		return 0;

	if (iocb->ki_flags & IOCB_APPEND)
		iocb->ki_pos = i_size_read(inode);

	if ((iocb->ki_flags & IOCB_NOWAIT) && !(iocb->ki_flags & IOCB_DIRECT))
		return -EINVAL;

	return generic_write_check_limits(iocb->ki_filp, iocb->ki_pos, count);
}

 
ssize_t generic_write_checks(struct kiocb *iocb, struct iov_iter *from)
{
	loff_t count = iov_iter_count(from);
	int ret;

	ret = generic_write_checks_count(iocb, &count);
	if (ret)
		return ret;

	iov_iter_truncate(from, count);
	return iov_iter_count(from);
}

 
int generic_file_rw_checks(struct file *file_in, struct file *file_out)
{
	struct inode *inode_in = file_inode(file_in);
	struct inode *inode_out = file_inode(file_out);

	 
	if (S_ISDIR(inode_in->i_mode) || S_ISDIR(inode_out->i_mode))
		return -EISDIR;
	if (!S_ISREG(inode_in->i_mode) || !S_ISREG(inode_out->i_mode))
		return -EINVAL;

	if (!(file_in->f_mode & FMODE_READ) ||
	    !(file_out->f_mode & FMODE_WRITE) ||
	    (file_out->f_flags & O_APPEND))
		return -EBADF;

	return 0;
}
